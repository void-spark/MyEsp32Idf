@ECHO OFF
docker run -it --rm -v "%cd%:/esp/project" -w /esp/project esp32-idf bash -c "export PYTHONPATH=/esp/project:$PYTHONPATH && pushd /esp/esp-idf/components/esptool_py/esptool/ && git checkout v2.4.1 && popd && cp esptool.py /esp/esp-idf/components/esptool_py/esptool/ && make -j8 all flash monitor"
