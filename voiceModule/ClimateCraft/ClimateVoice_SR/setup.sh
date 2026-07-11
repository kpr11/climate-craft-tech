source /home/rajesh/climate-craft/climate-craft-tech/voice_tool_chain/esp/esp-idf/export.sh
idf.py build
sudo usermod -a -G dialout $USER
sudo chmod 666 /dev/ttyACM0
rm -rf ~/climate_voice_log.txt
idf.py -p /dev/ttyACM0 flash monitor 2>&1 | tee -a ~/climate_voice_log.txt
