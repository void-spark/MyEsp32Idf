@ECHO OFF
docker run -it --rm -v "%cd%:/esp/project" -w /esp/project esp32-idf bash
