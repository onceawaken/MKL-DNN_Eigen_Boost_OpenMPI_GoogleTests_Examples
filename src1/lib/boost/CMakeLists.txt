project(Boost_Example_Lib VERSION 1.0.0 LANGUAGES C CXX)


if (BOOST_FOUND)

    if (PYTHON_FOUND)
        set(SOURCES
                main.python.hello.cpp
                )

        set(PYTHON_INIT
                __init__.py
                )

        set(BOOST_LIBS
                python27
                )

        add_module_boost_python(hello_ext "${SOURCES}" "${BOOST_LIBS}")

        add_python_init(hello_ext ${PYTHON_INIT})

        set(SOURCES
                main.python.numpy.cpp
                )

        set(PYTHON_INIT
                __init__.py
                )

        set(BOOST_LIBS
                python27
                numpy27
                )

        add_module_boost_python(boost_numpy "${SOURCES}" "${BOOST_LIBS}")
        add_python_init(boost_numpy ${PYTHON_INIT})
    endif ()

endif ()


