Packet SENDER:

1. flash code, openthread communication is created automatically.

   IF NOT

   -> idf.py menuconfig

   -> set these things: channel output to USB, input to CRLF, automatic openthread connection.

2. monitor code, packets should be sent automatically from #1 to #100.

Packet RECIEVER: 

1. flash code, openthread communication is created automatically.

   IF NOT

   -> idf.py menuconfig

   -> set these things: channel output to USB, input to CRLF, automatic openthread connection.

2. The channel state should be either (Leader,router) (if it isn't you might just need to wait a little for it to initialize, if it still isn't check if the channel is open or type in "ot udp open" and restart the sender

3. monitor code, packets should be recieved one by one. It checks the packets by order so 1,2,3 and if 1,2,4 it will log it as a missing packet.

