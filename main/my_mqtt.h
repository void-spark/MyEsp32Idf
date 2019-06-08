#ifndef MY_MQTT_H_
#define MY_MQTT_H_

void mqttStart(void (*messageHandlerArg)(char* topic, char* data));
void mqttWait();
void mqttPublish(const char *topic, const char *data, int len, int qos, int retain);
void publishDevProp(char *deviceProperty, char *value);
void publishNodeProp(char *nodeId, char *property, char *value);
void subscribeDevTopic(char *subTopic);

#endif
