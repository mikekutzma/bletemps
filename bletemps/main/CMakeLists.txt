idf_component_register(SRCS "bletemps.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_event
                        nvs_flash
                        json
                        driver
                        esp_timer
                        bt
)
