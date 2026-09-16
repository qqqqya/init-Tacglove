
uint8 STATE_IDLE=0          # 空闲 
uint8 STATE_READY=1         # 就绪 
uint8 STATE_ERROR=2         # 错误 
uint8 STATE_CALIBRATING=3   # 校准中     
uint8 STATE_UPDATING=4      # 固件更新中

std_msgs/Header header      
string firmware_version     # 固件版本 "预留" 
uint32 uptime_seconds       # 运行时长(秒) 
uint8 system_state          # 系统状态 
bool agent_connected        # 与PC端连接状态 
uint32 message_tx_count     # 发送消息计数     
uint32 message_rx_count     # 接收消息计数    


