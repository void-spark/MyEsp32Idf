idf_component_register(SRCS "bit.c"
                            "decoder.c"
                            "fixed.c"
                            "frame.c"
                            "huffman.c"
                            "layer12.c"
                            "layer3.c"
                            "stream.c"
                            "synth_stereo.c"
                            "timer.c"
                            "version.c"
                       INCLUDE_DIRS "include")
add_compile_definitions(HAVE_CONFIG_H)
