
uint8 STATE_IDLE=0
uint8 STATE_READY=1
uint8 STATE_ERROR=2
uint8 STATE_CALIBRATING=3
uint8 STATE_UPDATING=4

std_msgs/Header header
string firmware_version
uint32 uptime_seconds
uint8 system_state
bool agent_connected
uint32 message_tx_count
uint32 message_rx_count
