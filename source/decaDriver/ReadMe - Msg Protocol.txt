

Msg Sequence:
1) TX'er sends 'Poll' msg.
	- After TXing Poll, waits POLL_TX_TO_RESP_RX_DLY_UUS (150) then listens on RX port.

2) RX'er receives 'Poll':
	a) Reads RX times.
	b) Sets delay times.
	c) Sends 'Reply' message with frame counter value.
	d) After TXing Reply, waits RESP_TX_TO_FINAL_RX_DLY_UUS (500) then listens on RX port.

3) TX'er receives 'Reply' message.
	a) Reads RX & TX times.
	b) Sets delay times.
	c) Sends 'Final' message with frame counter value & timestamps.
	d) Wait til 'Final' is sent, then start over.

4) RX'er receives 'Final':
	a) Reads TX times.
	b) Gets timestamps from Final msg.
	c) Calculates distance.


