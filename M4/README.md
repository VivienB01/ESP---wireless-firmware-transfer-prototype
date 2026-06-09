## Milestone 4 ACK/NACK PROTOCOL

Reciever:
- After each packet reciever returnes ACK
- if its the incorrect, returns NACK

Sender
- Sending each packet after recieving the ACK from the previous packet
- If recieved NACK retries 3 times it fails
- packet number 21 is faulty on purpose to test the NACK/fail system
