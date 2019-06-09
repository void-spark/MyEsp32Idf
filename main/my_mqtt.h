#ifndef MY_MQTT_H_
#define MY_MQTT_H_

typedef void (* message_handler_t)(const char* topic, const char* data);

void mqttStart(message_handler_t messageHandlerArg);
void mqttWait();
void mqttPublish(const char *topic, const char *data, int len, int qos, int retain);
void publishDevProp(const char *deviceProperty, const char *value);
void publishNodeProp(const char *nodeId, const char *property, const char *value);
void subscribeDevTopic(const char *subTopic);

#endif
