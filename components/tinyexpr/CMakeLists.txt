idf_component_register(SRCS "tinyexpr.c"
                    INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=char-subscripts)
