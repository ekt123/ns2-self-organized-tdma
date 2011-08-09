Agent set dst_ipaddr_ 0
Agent set dst_ipport_ -1
Agent set my_ipaddr_ 0
Agent set my_port_ -1

RTILink set delay_ 100ms
RTILink set bandwidth_ 1.5Mb
RTILink set ipaddr_ 0
RTILink set netmask_ 0
RTILink set debug_ 0
RTILink set off_ip_ 0
RTILink set avoidReordering_ false; # ALFRED for ns-2.27, not used

DropTargetAgent set debug_ 0

if [TclObject is-class Agent/TCP/Listener] {
  Agent/TCP/Listener instproc done_data {} { }
  Agent/TCP/Listener set debug_ 1; # Default debug level, 0 = off, 3 = max
  Agent/TCP/Listener set trace_enable_ 0; # Trace level for Net Anim, 0 = off,
                                          # 1 = syn/table, 2 = synack, 3 = max
  Agent/TCP/Listener set trace_interval_ 10; # Trace every nth packet
  Agent/TCP/Listener set max_synack_retries_ 0; # Number of SYN+ACK 
                                                # retries, [0:disable]
  Agent/TCP/Listener set table_size_ 256; # TCP connection table size
  Agent/TCP/Listener set entry_expire_ 6.0; # TCP connection table entry
                           # expiration time after intial SYN, enabled only 
                           # when max_synack_retries_ == 0 (disabled)
  Agent/TCP/Listener set filter_enable_ 0; # Egress filtering [0:disable]
  Agent/TCP/Listener set filter_trigger_ 0.75; # filter trigger threshold 
  Agent/TCP/Listener set filter_release_ 0.50; # filter release threshold 
  Agent/TCP/Listener set prebinding_ 0; # FullTcpAgent prebinding support
	Agent/TCP/Listener set segsperack_ 1; # ACK frequency
	Agent/TCP/Listener set spa_thresh_ 0; # below do 1 seg per ack [0:disable]
	Agent/TCP/Listener set segsize_ 536; # segment size
	Agent/TCP/Listener set tcprexmtthresh_ 3; # num dupacks to enter recov
	Agent/TCP/Listener set iss_ 0; # Initial send seq#
	Agent/TCP/Listener set nodelay_ false; # Nagle disable?
	Agent/TCP/Listener set data_on_syn_ false; # allow data on 1st SYN?
	Agent/TCP/Listener set dupseg_fix_ true ; # no rexmt w/dup segs from peer
	Agent/TCP/Listener set dupack_reset_ false; # exit recov on ack < highest
	Agent/TCP/Listener set interval_ 0.1 ; # delayed ACK interval 100ms 
	Agent/TCP/Listener set close_on_empty_ false; # close conn if sent all
	Agent/TCP/Listener set signal_on_empty_ false; # signal if sent all
	Agent/TCP/Listener set ts_option_size_ 10; # in bytes
	Agent/TCP/Listener set reno_fastrecov_ true; # fast recov true by default
	Agent/TCP/Listener set pipectrl_ false; # use "pipe" ctrl
	Agent/TCP/Listener set open_cwnd_on_pack_ true; # ^ win on partial acks?
	Agent/TCP/Listener set halfclose_ false; # do simplex closes (shutdown)?
	Agent/TCP/Listener set nopredict_ false; # disable header prediction code?
}

