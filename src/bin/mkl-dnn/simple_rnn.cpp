/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <cstring>
#include <iostream>
#include <math.h>
#include <numeric>
#include <string>

#include "mkldnn.hpp"

// MSVC doesn't support collapse clause in omp parallel
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__INTEL_COMPILER)
#define collapse(x)
#endif

using namespace mkldnn;

const int batch = 128;
const int src_seq_length_max = 28;
const int tgt_seq_length_max = 28;

const int feature_size = 1024;

const int enc_bidir_n_layers = 1;
const int enc_unidir_n_layers = 7;
const int dec_n_layers = 8;

const int lstm_n_gates = 4;
const int lstm_n_states = 2;
std::vector<float> weighted_src_layer(batch *feature_size, 1.0f);
std::vector<float> alignment_model(
        src_seq_length_max *batch *feature_size, 1.0f);
std::vector<float> alignments(src_seq_length_max *batch, 1.0f);
std::vector<float> exp_sums(batch, 1.0f);

const float onef = 1.0, zerof = 0.0;
const int onei = 1;

void compute_weighted_annotations(float *weighted_annotations,
        int src_seq_length_max, int batch, int feature_size,
        float *weights_annot, float *annotations) {
    // annotations(aka enc_dst_layer) is (t, n, 2c)
    // weights_annot is (2c, c)

    // annotation[i] = GEMM(weights_annot, enc_dst_layer[i]);
    int num_weighted_annotations = src_seq_length_max * batch;
    mkldnn_sgemm("N", "N",
            &feature_size, &num_weighted_annotations, &feature_size,
            &onef, weights_annot, &feature_size, annotations, &feature_size,
            &zerof, weighted_annotations, &feature_size);
}

void compute_attention(float *context_vectors, int src_seq_length_max,
        int batch, int feature_size, float *weights_src_layer,
        float *dec_src_layer, float *annotations, float *weighted_annotations,
        float *weights_alignments) {
    // dst_iter : (n, c) matrix
    // src_layer: (n, c) matrix
    // weighted_annotations (t, n, c)

    // weights_yi is (c, c)
    // weights_ai is (c, 1)
    // tmp_old_ompi_project[i] is (n, c)
    // a[i] is (n, 1)
    // p is (n, 1)

    // first we precompute the weighted_dec_src_layer
    mkldnn_sgemm("N", "N",
            &feature_size, &batch, &feature_size, &onef,
            weights_src_layer, &feature_size, dec_src_layer, &feature_size,
            &zerof, weighted_src_layer.data(), &feature_size);

    // then we compute the alignment model
    float *alignment_model_ptr = alignment_model.data();
#ifdef _OPENMP
#pragma omp parallel for collapse(2)
#endif
    for (int i = 0; i < src_seq_length_max; i++) {
        for (int j = 0; j < batch * feature_size; j++)
            alignment_model_ptr[i * batch * feature_size + j] = tanhf(
                    weighted_src_layer.data()[j]
                    + weighted_annotations[i * batch * feature_size + j]);
    }

    // gemv with alignments weights. the resulting alignments are in alignments
    int num_weighted_annotations = src_seq_length_max * batch;
    mkldnn_sgemm("N", "N",
            &onei, &num_weighted_annotations, &feature_size, &onef,
            weights_alignments, &onei, alignment_model_ptr, &feature_size,
            &zerof, alignments.data(), &onei);

    // softmax on alignments. the resulting context weights are in alignments
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int i = 0; i < batch; i++)
        exp_sums[i] = 0.0f;
#ifdef _OPENMP
#pragma omp parallel for collapse(2)
#endif
    for (int i = 0; i < src_seq_length_max; i++) {
        for (int j = 0; j < batch; j++) {
            alignments[i * batch + j] = expf(alignments[i * batch + j]);
            exp_sums[j] += alignments[i * batch + j];
        }
    }

#ifdef _OPENMP
#pragma omp parallel for collapse(2)
#endif
    for (int i = 0; i < src_seq_length_max; i++)
        for (int j = 0; j < batch; j++)
            alignments[i * batch + j] /= exp_sums[j];

    // then we compute the context vectors
#ifdef _OPENMP
#pragma omp parallel for collapse(2)
#endif
    for (int i = 0; i < batch; i++)
        for (int j = 0; j < feature_size; j++)
            context_vectors[i * (feature_size + feature_size) + feature_size
                    + j]
                    = 0.0f;

#ifdef _OPENMP
#pragma omp parallel for collapse(3)
#endif
    for (int i = 0; i < batch; i++)
        for (int k = 0; k < src_seq_length_max; k++)
            for (int j = 0; j < feature_size; j++)
                context_vectors[i * (feature_size + feature_size) + feature_size
                        + j]
                        += alignments[k * batch + i]
                        * annotations[j + feature_size * (i + batch * k)];
}

void copy_context(float *src_iter, int n_layers, int n_states, int batch,
        int feature_size) {
    // we copy the context from the first layer to all other layers
#ifdef _OPENMP
#pragma omp parallel for collapse(3)
#endif
    for (int k = 1; k < n_layers; k++)
        for (int j = 0; j < batch; j++)
            for (int i = 0; i < feature_size; i++)
                src_iter[(k * n_states * batch + j)
                                * (feature_size + feature_size)
                        + i]
                        = src_iter[j * (feature_size + feature_size) + i];
}

void simple_net() {
    auto cpu_engine = engine(engine::cpu, 0);
    auto null_memory_ = null_memory(cpu_engine);

    /*
      GNMT Example.
      Note, we do not implement connection yet.
      For the encoder we use:
      - one primitive for the bidirectional layer of the encoder
      - one primitive for all remaining unidirectional layers in the encoder
      For the decoder we use:
      - one primitive for the first iteration
      - one primitive for all subsequent iterations in the decoder. Note that
        in this example, this primitive computes the states in place.
      - the attention mechanism is implemented separately as there is no support
        for the context vectors in MKL_DNN yet
     */

    std::vector<primitive> weights_reorders;
    std::vector<primitive> encoder_net;
    std::vector<primitive> decoder_net;

    std::vector<float> net_src(batch * src_seq_length_max * feature_size, 1.0f);
    std::vector<float> net_dst(batch * tgt_seq_length_max * feature_size, 1.0f);

    /* Encoder */

    memory::dims enc_bidir_src_layer_tz
            = { src_seq_length_max, batch, feature_size };
    memory::dims enc_bidir_weights_layer_tz = { enc_bidir_n_layers, 2,
        feature_size, lstm_n_gates, feature_size };
    memory::dims enc_bidir_weights_iter_tz = { enc_bidir_n_layers, 2,
        feature_size, lstm_n_gates, feature_size };
    memory::dims enc_bidir_bias_tz
            = { enc_bidir_n_layers, 2, lstm_n_gates, feature_size };
    memory::dims enc_bidir_dst_layer_tz
            = { src_seq_length_max, batch, 2 * feature_size };

    /* GNMT encoder: 1 bidirectional layer and 7 unidirectional layers */

    std::vector<float> user_enc_bidir_wei_layer(
            enc_bidir_n_layers * 2 * feature_size * lstm_n_gates * feature_size,
            1.0f);
    std::vector<float> user_enc_bidir_wei_iter(
            enc_bidir_n_layers * 2 * feature_size * lstm_n_gates * feature_size,
            1.0f);
    std::vector<float> user_enc_bidir_bias(
            enc_bidir_n_layers * 2 * lstm_n_gates * feature_size, 1.0f);

    /* Create the memory for user data */
    auto user_enc_bidir_src_layer_md = mkldnn::memory::desc(
            { enc_bidir_src_layer_tz }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::tnc);

    auto user_enc_bidir_wei_layer_md = mkldnn::memory::desc(
            { enc_bidir_weights_layer_tz }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldigo);

    auto user_enc_bidir_wei_iter_md = mkldnn::memory::desc(
            { enc_bidir_weights_iter_tz }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldigo);

    auto user_enc_bidir_bias_md = mkldnn::memory::desc({ enc_bidir_bias_tz },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldgo);

    auto user_enc_bidir_src_layer_memory = mkldnn::memory(
            { user_enc_bidir_src_layer_md, cpu_engine }, net_src.data());
    auto user_enc_bidir_wei_layer_memory
            = mkldnn::memory({ user_enc_bidir_wei_layer_md, cpu_engine },
                    user_enc_bidir_wei_layer.data());
    auto user_enc_bidir_wei_iter_memory
            = mkldnn::memory({ user_enc_bidir_wei_iter_md, cpu_engine },
                    user_enc_bidir_wei_iter.data());
    auto user_enc_bidir_bias_memory = mkldnn::memory(
            { user_enc_bidir_bias_md, cpu_engine }, user_enc_bidir_bias.data());

    /* Create memory descriptors for RNN data w/o specified layout */
    auto enc_bidir_wei_layer_md = memory::desc({ enc_bidir_weights_layer_tz },
            memory::data_type::f32, memory::format::any);

    auto enc_bidir_wei_iter_md = memory::desc({ enc_bidir_weights_iter_tz },
            memory::data_type::f32, memory::format::any);

    auto enc_bidir_dst_layer_md = memory::desc({ enc_bidir_dst_layer_tz },
            memory::data_type::f32, memory::format::any);

    /* Create bidirectional RNN */
    rnn_cell::desc bi_cell(algorithm::vanilla_lstm);
    rnn_forward::desc bi_layer_desc(prop_kind::forward_inference, bi_cell,
            rnn_direction::bidirectional_concat, user_enc_bidir_src_layer_md,
            zero_md(), enc_bidir_wei_layer_md, enc_bidir_wei_iter_md,
            user_enc_bidir_bias_md, enc_bidir_dst_layer_md, zero_md());

    auto enc_bidir_prim_desc
            = mkldnn::rnn_forward::primitive_desc(bi_layer_desc, cpu_engine);

    /* Create memory primitives for input data and use reorders to reorder
     * user data to internal representation
     */
    auto enc_bidir_wei_layer_memory
            = memory(enc_bidir_prim_desc.weights_layer_primitive_desc());
    auto enc_bidir_wei_layer_reorder_pd = reorder::primitive_desc(
            user_enc_bidir_wei_layer_memory.get_primitive_desc(),
            enc_bidir_wei_layer_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_bidir_wei_layer_reorder_pd,
            user_enc_bidir_wei_layer_memory, enc_bidir_wei_layer_memory));

    auto enc_bidir_wei_iter_memory
            = memory(enc_bidir_prim_desc.weights_iter_primitive_desc());
    auto enc_bidir_wei_iter_reorder_pd = reorder::primitive_desc(
            user_enc_bidir_wei_iter_memory.get_primitive_desc(),
            enc_bidir_wei_iter_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_bidir_wei_iter_reorder_pd,
            user_enc_bidir_wei_iter_memory, enc_bidir_wei_iter_memory));

    auto enc_bidir_dst_layer_memory
            = mkldnn::memory(enc_bidir_prim_desc.dst_layer_primitive_desc());

    encoder_net.push_back(
            rnn_forward(enc_bidir_prim_desc, user_enc_bidir_src_layer_memory,
                    null_memory_, enc_bidir_wei_layer_memory,
                    enc_bidir_wei_iter_memory, user_enc_bidir_bias_memory,
                    enc_bidir_dst_layer_memory, null_memory_, null_memory_));

    /* GNMT encoder: unidirectional layers */
    // First unidirectinal layer scales 2 * feature_size output of bidirectional
    // layer to feature_size output
    std::vector<float> user_enc_uni_first_wei_layer(
            1 * 1 * 2 * feature_size * lstm_n_gates * feature_size, 1.0f);
    std::vector<float> user_enc_uni_first_wei_iter(
            1 * 1 * feature_size * lstm_n_gates * feature_size, 1.0f);
    std::vector<float> user_enc_uni_first_bias(
            1 * 1 * lstm_n_gates * feature_size, 1.0f);
    memory::dims user_enc_uni_first_wei_layer_dims
            = { 1, 1, 2 * feature_size, lstm_n_gates, feature_size };
    memory::dims user_enc_uni_first_wei_iter_dims
            = { 1, 1, feature_size, lstm_n_gates, feature_size };
    memory::dims user_enc_uni_first_bias_dims
            = { 1, 1, lstm_n_gates, feature_size };
    memory::dims enc_uni_first_dst_layer_dims
            = { src_seq_length_max, batch, feature_size };
    auto user_enc_uni_first_wei_layer_md = mkldnn::memory::desc(
            { user_enc_uni_first_wei_layer_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldigo);
    auto user_enc_uni_first_wei_iter_md = mkldnn::memory::desc(
            { user_enc_uni_first_wei_iter_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldigo);
    auto user_enc_uni_first_bias_md = mkldnn::memory::desc(
            { user_enc_uni_first_bias_dims }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldgo);
    auto user_enc_uni_first_wei_layer_memory
            = mkldnn::memory({ user_enc_uni_first_wei_layer_md, cpu_engine },
                    user_enc_uni_first_wei_layer.data());
    auto user_enc_uni_first_wei_iter_memory
            = mkldnn::memory({ user_enc_uni_first_wei_iter_md, cpu_engine },
                    user_enc_uni_first_wei_iter.data());
    auto user_enc_uni_first_bias_memory
            = mkldnn::memory({ user_enc_uni_first_bias_md, cpu_engine },
                    user_enc_uni_first_bias.data());

    auto enc_uni_first_wei_layer_md
            = memory::desc({ user_enc_uni_first_wei_layer_dims },
                    memory::data_type::f32, memory::format::any);
    auto enc_uni_first_wei_iter_md
            = memory::desc({ user_enc_uni_first_wei_iter_dims },
                    memory::data_type::f32, memory::format::any);
    auto enc_uni_first_dst_layer_md
            = memory::desc({ enc_uni_first_dst_layer_dims },
                    memory::data_type::f32, memory::format::any);

    /// @todo add suport for residual connections
    /// should it be a set residual in op_desc or a field to set manually?
    /// should be an integer to specify at which layer to start
    rnn_cell::desc enc_uni_first_cell(algorithm::vanilla_lstm);
    rnn_forward::desc enc_uni_first_layer_desc(prop_kind::forward_inference,
            enc_uni_first_cell, rnn_direction::unidirectional_left2right,
            enc_bidir_dst_layer_md, zero_md(), enc_uni_first_wei_layer_md,
            enc_uni_first_wei_iter_md, user_enc_uni_first_bias_md,
            enc_uni_first_dst_layer_md, zero_md());
    auto enc_uni_first_prim_desc = mkldnn::rnn_forward::primitive_desc(
            enc_uni_first_layer_desc, cpu_engine);

    auto enc_uni_first_wei_layer_memory
            = memory(enc_uni_first_prim_desc.weights_layer_primitive_desc());
    auto enc_uni_first_wei_layer_reorder_pd = reorder::primitive_desc(
            user_enc_uni_first_wei_layer_memory.get_primitive_desc(),
            enc_uni_first_wei_layer_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_uni_first_wei_layer_reorder_pd,
            user_enc_uni_first_wei_layer_memory,
            enc_uni_first_wei_layer_memory));

    auto enc_uni_first_wei_iter_memory
            = memory(enc_uni_first_prim_desc.weights_iter_primitive_desc());
    auto enc_uni_first_wei_iter_reorder_pd = reorder::primitive_desc(
            user_enc_uni_first_wei_iter_memory.get_primitive_desc(),
            enc_uni_first_wei_iter_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_uni_first_wei_iter_reorder_pd,
            user_enc_uni_first_wei_iter_memory, enc_uni_first_wei_iter_memory));

    auto enc_uni_first_dst_layer_memory = mkldnn::memory(
            enc_uni_first_prim_desc.dst_layer_primitive_desc());

    encoder_net.push_back(rnn_forward(enc_uni_first_prim_desc,
            enc_bidir_dst_layer_memory, null_memory_,
            enc_uni_first_wei_layer_memory,
            enc_uni_first_wei_iter_memory, user_enc_uni_first_bias_memory,
            enc_uni_first_dst_layer_memory, null_memory_, null_memory_));

    /* Remainging unidirectional layers */
    std::vector<float> user_enc_uni_wei_layer((enc_unidir_n_layers - 1) * 1
                    * feature_size * lstm_n_gates * feature_size, 1.0f);
    std::vector<float> user_enc_uni_wei_iter((enc_unidir_n_layers - 1) * 1
                    * feature_size * lstm_n_gates * feature_size, 1.0f);
    std::vector<float> user_enc_uni_bias(
            (enc_unidir_n_layers - 1) * 1 * lstm_n_gates * feature_size, 1.0f);
    memory::dims user_enc_uni_wei_layer_dims = { (enc_unidir_n_layers - 1), 1,
        feature_size, lstm_n_gates, feature_size };
    memory::dims user_enc_uni_wei_iter_dims = { (enc_unidir_n_layers - 1), 1,
        feature_size, lstm_n_gates, feature_size };
    memory::dims user_enc_uni_bias_dims
            = { (enc_unidir_n_layers - 1), 1, lstm_n_gates, feature_size };
    memory::dims enc_dst_layer_dims
            = { src_seq_length_max, batch, feature_size };
    auto user_enc_uni_wei_layer_md = mkldnn::memory::desc(
            { user_enc_uni_wei_layer_dims }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldigo);
    auto user_enc_uni_wei_iter_md = mkldnn::memory::desc(
            { user_enc_uni_wei_iter_dims }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldigo);
    auto user_enc_uni_bias_md = mkldnn::memory::desc({ user_enc_uni_bias_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldgo);
    auto user_enc_uni_wei_layer_memory
            = mkldnn::memory({ user_enc_uni_wei_layer_md, cpu_engine },
                    user_enc_uni_wei_layer.data());
    auto user_enc_uni_wei_iter_memory
            = mkldnn::memory({ user_enc_uni_wei_iter_md, cpu_engine },
                    user_enc_uni_wei_iter.data());
    auto user_enc_uni_bias_memory = mkldnn::memory(
            { user_enc_uni_bias_md, cpu_engine }, user_enc_uni_bias.data());

    auto enc_uni_wei_layer_md = memory::desc({ user_enc_uni_wei_layer_dims },
            memory::data_type::f32, memory::format::any);
    auto enc_uni_wei_iter_md = memory::desc({ user_enc_uni_wei_iter_dims },
            memory::data_type::f32, memory::format::any);
    auto enc_dst_layer_md = memory::desc({ enc_dst_layer_dims },
            memory::data_type::f32, memory::format::any);

    /// @todo add suport for residual connections
    /// should it be a set residual in op_desc or a field to set manually?
    /// should be an integer to specify at which layer to start
    rnn_cell::desc enc_uni_cell(algorithm::vanilla_lstm);
    rnn_forward::desc enc_uni_layer_desc(prop_kind::forward_inference,
            enc_uni_cell, rnn_direction::unidirectional_left2right,
            enc_uni_first_dst_layer_md, zero_md(), enc_uni_wei_layer_md,
            enc_uni_wei_iter_md, user_enc_uni_bias_md, enc_dst_layer_md,
            zero_md());
    auto enc_uni_prim_desc = mkldnn::rnn_forward::primitive_desc(
            enc_uni_layer_desc, cpu_engine);

    auto enc_uni_wei_layer_memory
            = memory(enc_uni_prim_desc.weights_layer_primitive_desc());
    auto enc_uni_wei_layer_reorder_pd = reorder::primitive_desc(
            user_enc_uni_wei_layer_memory.get_primitive_desc(),
            enc_uni_wei_layer_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_uni_wei_layer_reorder_pd,
            user_enc_uni_wei_layer_memory, enc_uni_wei_layer_memory));

    auto enc_uni_wei_iter_memory
            = memory(enc_uni_prim_desc.weights_iter_primitive_desc());
    auto enc_uni_wei_iter_reorder_pd = reorder::primitive_desc(
            user_enc_uni_wei_iter_memory.get_primitive_desc(),
            enc_uni_wei_iter_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(enc_uni_wei_iter_reorder_pd,
            user_enc_uni_wei_iter_memory, enc_uni_wei_iter_memory));

    auto enc_dst_layer_memory
            = mkldnn::memory(enc_uni_prim_desc.dst_layer_primitive_desc());

    encoder_net.push_back(
            rnn_forward(enc_uni_prim_desc, enc_uni_first_dst_layer_memory,
                    null_memory_, enc_uni_wei_layer_memory,
                    enc_uni_wei_iter_memory, user_enc_uni_bias_memory,
                    enc_dst_layer_memory, null_memory_, null_memory_));

    /* GNMT: decoder with attention mechanism */
    std::vector<float> user_dec_wei_layer(
            dec_n_layers * 1 * feature_size * lstm_n_gates * feature_size,
            1.0f);
    std::vector<float> user_dec_wei_iter(dec_n_layers * 1
                    * (feature_size + feature_size) * lstm_n_gates
                    * feature_size, 1.0f);
    std::vector<float> user_dec_bias(
            dec_n_layers * 1 * lstm_n_gates * feature_size, 1.0f);
    std::vector<float> user_dec_dst(
            tgt_seq_length_max * batch * feature_size, 1.0f);
    std::vector<float> user_weights_attention_src_layer(
            feature_size * feature_size, 1.0f);
    std::vector<float> user_weights_annotation(
            feature_size * feature_size, 1.0f);
    std::vector<float> user_weights_alignments(feature_size, 1.0f);

    memory::dims user_dec_wei_layer_dims
            = { dec_n_layers, 1, feature_size, lstm_n_gates, feature_size };
    memory::dims user_dec_wei_iter_dims = { dec_n_layers, 1,
        feature_size + feature_size, lstm_n_gates, feature_size };
    memory::dims user_dec_bias_dims
            = { dec_n_layers, 1, lstm_n_gates, feature_size };

    memory::dims dec_src_layer_dims = { 1, batch, feature_size };
    memory::dims dec_dst_layer_dims = { 1, batch, feature_size };

    // We will use the same memory for dec_src_iter and dec_dst_iter
    // However, dec_src_iter has a context vector but not
    // dec_dst_iter.
    // To resolve this we will create one memory that holds the
    // context vector as well as the both the hidden and cell states.
    // For the dst_iter, we will use a view on this memory.
    // Note that the cell state will be padded by
    // feature_size values. However, we do not compute or
    // access those.
    memory::dims dec_dst_iter_dims = { dec_n_layers, 1, lstm_n_states, batch,
        feature_size + feature_size };
    memory::dims dec_dst_iter_noctx_dims
            = { dec_n_layers, 1, lstm_n_states, batch, feature_size };

    auto user_dec_wei_layer_md = mkldnn::memory::desc(
            { user_dec_wei_layer_dims }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::ldigo);
    auto user_dec_wei_iter_md = mkldnn::memory::desc({ user_dec_wei_iter_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldigo);
    auto user_dec_bias_md = mkldnn::memory::desc({ user_dec_bias_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldgo);
    auto dec_dst_layer_md = mkldnn::memory::desc({ dec_dst_layer_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::tnc);
    auto dec_src_layer_md = mkldnn::memory::desc({ dec_src_layer_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::tnc);
    auto dec_dst_iter_md = mkldnn::memory::desc({ dec_dst_iter_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::ldsnc);
    auto user_dec_wei_layer_memory = mkldnn::memory(
            { user_dec_wei_layer_md, cpu_engine }, user_dec_wei_layer.data());
    auto user_dec_wei_iter_memory = mkldnn::memory(
            { user_dec_wei_iter_md, cpu_engine }, user_dec_wei_iter.data());
    auto user_dec_bias_memory = mkldnn::memory(
            { user_dec_bias_md, cpu_engine }, user_dec_bias.data());
    auto user_dec_dst_layer_memory = mkldnn::memory(
            { dec_dst_layer_md, cpu_engine }, user_dec_dst.data());
    auto dec_src_layer_memory
            = mkldnn::memory({ dec_src_layer_md, cpu_engine });

    auto dec_wei_layer_md = mkldnn::memory::desc(
            { user_dec_wei_layer_dims }, mkldnn::memory::data_type::f32,
            mkldnn::memory::format::any);
    auto dec_wei_iter_md = mkldnn::memory::desc({ user_dec_wei_iter_dims },
            mkldnn::memory::data_type::f32, mkldnn::memory::format::any);

    // As mentioned above, we create a view without context out of the
    // memory with context.
    auto dec_dst_iter_memory = mkldnn::memory({ dec_dst_iter_md, cpu_engine });
    auto dec_dst_iter_noctx_md = mkldnn::view::primitive_desc(
            dec_dst_iter_memory.get_primitive_desc(), dec_dst_iter_noctx_dims,
            { 0, 0, 0, 0, 0 }).dst_primitive_desc().desc();

    /// @todo add suport for residual connections
    /// should it be a set residual in op_desc or a field to set manually?
    /// should be an integer to specify at which layer to start
    rnn_cell::desc dec_cell(algorithm::vanilla_lstm);
    rnn_forward::desc dec_ctx_desc(prop_kind::forward_inference, dec_cell,
            rnn_direction::unidirectional_left2right, dec_src_layer_md,
            dec_dst_iter_md, dec_wei_layer_md, dec_wei_iter_md,
            user_dec_bias_md, dec_dst_layer_md, dec_dst_iter_noctx_md);
    auto dec_ctx_prim_desc
            = mkldnn::rnn_forward::primitive_desc(dec_ctx_desc, cpu_engine);

    auto dec_wei_layer_memory
            = memory(dec_ctx_prim_desc.weights_layer_primitive_desc());
    auto dec_wei_layer_reorder_pd = reorder::primitive_desc(
            user_dec_wei_layer_memory.get_primitive_desc(),
            dec_wei_layer_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(dec_wei_layer_reorder_pd,
            user_dec_wei_layer_memory, dec_wei_layer_memory));

    auto dec_wei_iter_memory
            = memory(dec_ctx_prim_desc.weights_iter_primitive_desc());
    auto dec_wei_iter_reorder_pd = reorder::primitive_desc(
            user_dec_wei_iter_memory.get_primitive_desc(),
            dec_wei_iter_memory.get_primitive_desc());
    weights_reorders.push_back(reorder(dec_wei_iter_reorder_pd,
            user_dec_wei_iter_memory, dec_wei_iter_memory));

    decoder_net.push_back(rnn_forward(dec_ctx_prim_desc, dec_src_layer_memory,
            dec_dst_iter_memory, dec_wei_layer_memory,
            dec_wei_iter_memory, user_dec_bias_memory,
            user_dec_dst_layer_memory, dec_dst_iter_memory, null_memory_));

    // allocating temporary buffer for attention mechanism
    std::vector<float> weighted_annotations(
            src_seq_length_max * batch * feature_size, 1.0f);

    /*
       Execution
     */
    auto execute = [&]() {
        // reorder weights to MKLDNN internal representation
        stream(stream::kind::eager).submit(weights_reorders).wait();

        // run encoder (1 stream)
        stream(stream::kind::eager).submit(encoder_net).wait();

        // we compute the weighted annotations once before the decoder
        compute_weighted_annotations(weighted_annotations.data(),
                src_seq_length_max, batch, feature_size,
                user_weights_annotation.data(),
                (float *)enc_dst_layer_memory.get_data_handle());

        // We initialise src_layer to the embedding of </s>, which
        // are assumed to be 0 here
        memset(dec_src_layer_memory.get_data_handle(), 0,
               dec_src_layer_memory.get_primitive_desc().get_size());
        // From now on, src points to the output of the last iteration

        for (int i = 0; i < tgt_seq_length_max; i++) {
            float *src_att_layer_handle
                    = (float *) dec_src_layer_memory.get_data_handle();
            float *src_att_iter_handle
                    = (float *) dec_dst_iter_memory.get_data_handle();

            // Compute attention context vector into the first layer src_iter
            compute_attention(src_att_iter_handle, src_seq_length_max, batch,
                    feature_size, user_weights_attention_src_layer.data(),
                    src_att_layer_handle,
                    (float *)enc_bidir_dst_layer_memory.get_data_handle(),
                    weighted_annotations.data(),
                    user_weights_alignments.data());

            // copy the context vectors to all layers of src_iter
            copy_context(src_att_iter_handle, dec_n_layers, lstm_n_states, batch,
                    feature_size);

            // run the decoder iteration
            stream(stream::kind::eager).submit(decoder_net).wait();

            // Move the handle on the src/dst layer to the next iteration
            auto dst_layer_handle = (float *) user_dec_dst_layer_memory.get_data_handle();
            dec_src_layer_memory.set_data_handle(dst_layer_handle);
            user_dec_dst_layer_memory.set_data_handle(
                    dst_layer_handle + batch * feature_size);
        }

    };

    execute();
}

int main(int argc, char **argv) {
    try {
        simple_net();
        std::cout << "ok\n";
    } catch (error &e) {
        std::cerr << "status: " << e.status << std::endl;
        std::cerr << "message: " << e.message << std::endl;
        return 1;
    }
    return 0;
}
