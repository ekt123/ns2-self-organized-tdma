#define options for wireless system

#define channel type
set val(chan) Channel/WirelessChannel
#define radio propagation model
set val(prop) Propagation/TwoRayGround
#define physical interphase
set val(netif) Phy/WirelessPhy
#define MAC layer
set val(mac) Mac/802_11
#define packet queue model
set val(ifq) Queue/DropTail/PriQueue
#define link layer type
set val(ll) LL
#define antenna model
set val(ant) Antenna/OmniAntenna
#define queue size
set val(ifqlen) 50
#define number of mobile nodes
set val(nn) 2
#define routing protocol
#DSDV is ad-hoc
set val(rp) DSDV

#Main program

#Initialize global variables
set ns_ [new Simulator]
set tracefd [open w1ex.tr w]
$ns_ trace-all $tracefd



#set up  topography 500x500 m resolution is 1m
set topo [new Topography]

$topo load_flatgrid 500 500

#Create God
#
# God (General Operations Director) is the object that is used to store global 
# information about the state of the environment, network or nodes that an 
# omniscent observer would have, but that should not be made known to any 
# participant in the simulation

create-god $val(nn)

#
#  Create the specified number of mobilenodes [$val(nn)] and "attach" them
#  to the channel. 

# global configuration of nodes
$ns_ node-config -adhocRouting $val(rp) \
		 -llType $val(ll) \
		 -macType $val(mac) \
		 -ifqType $val(ifq) \
		 -ifqLen $val(ifqlen) \
		 -antType $val(ant) \
		 -propType $val(prop) \
		 -phyType $val(netif) \
		 -channelType $val(chan) \
		 -topoInstance $topo \
		 -agentTrace ON \
		 -routerTrace ON \
		 -macTrace OFF \
		 -movementTrace OFF
		 
for {set i 0} {$i < $val(nn) } {incr i} {
 set node_($i) [$ns_ node]
 $node_($i) random-motion 0 ;#disable random motion
}

# Define initial position of each node
# We assume that two nodes are at the same height (Z=0) at the beginning

$node_(0) set X_ 5.0
$node_(0) set Y_ 2.0
$node_(0) set Z_ 0.0

$node_(1) set X_ 390.0
$node_(1) set Y_ 385.0
$node_(1) set Z_ 0.0

# Let's move node_1 toward node_0
$ns_ at 50.0 "$node_(1) setdest 25.0 20.0 15.0"
$ns_ at 10.0 "$node_(0) setdest 20.0 18.0 1.0"

# Node_1 then move away from node_0
$ns_ at 100.0 "$node_(1) setdest 490.0 480.0 15.0"

#Setup TCP traffic flow
set tcp [new Agent/TCP]
$tcp set class_ 2
set sink [new Agent/TCPSink]
$ns_ attach-agent $node_(0) $tcp
$ns_ attach-agent $node_(1) $sink
$ns_ connect $tcp $sink
set ftp [new Application/FTP]
$ftp attach-agent $tcp
$ns_ at 10.0 "$ftp start"

#
# Tell nodes when the simulation ends
#
for {set i 0} {$i < $val(nn) } {incr i} {
    $ns_ at 150.0 "$node_($i) reset";
}
$ns_ at 150.0 "stop"
$ns_ at 150.01 "puts \"NS EXITING...\" ; $ns_ halt"
proc stop {} {
    global ns_ tracefd
    $ns_ flush-trace
    close $tracefd
    
}

puts "Starting Simulation..."
$ns_ run



