#ifndef MY_MQTT_H_
#define MY_MQTT_H_

void mqttStart();
void mqttWait();
void mqttPublish(const char *topic, const char *data, int len, int qos, int retain);

#endif
