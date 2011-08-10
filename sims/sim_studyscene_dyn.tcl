#Simulation Testing: initialization and net entry phase
#
#Simulation Setup
# 16 mobile nodes, start 1 node at a time and measure net entry time  
# All nodes transmit position report message


#remove unnescessary headers 
remove-all-packet-headers
#add require header i.e. IP
add-packet-header IP ARP LL Mac HdrNbInfo TdlData TdlMsgUpdate TdlNetUpdate NetEntryMsg PollingMsg ControlMsg

# ======================================================================

# Define options

# ======================================================================




set val(chan)           Channel/WirelessChannel    ;# channel type

set val(prop)           Propagation/TwoRayGround   ;# radio-propagation model

set val(netif)          Phy/WirelessPhy            ;# network interface type

set val(mac)            Mac/DynamicTdma            ;# MAC type

set val(ifq)            Queue/DropTail/PriQueue    ;# interface queue type

set val(ll)             LL                         ;# link layer type

set val(ant)            Antenna/OmniAntenna        ;# antenna model

set val(ifqlen)         100                        ;# max packet in ifq

set val(nn)             7                         ;# number of mobilenodes

set val(rp)             NOAH                       ;# routing protocol

set val(x)		5000000			   ;# X dimension of topology

set val(y)		5000000			   ;# Y dimension of topology

#set channel bandwidth
Mac set bandwidth_ 5e4
#Mac/FixTdma set slot_packet_len_ 300
#Mac/FixTdma set max_slot_num_	200
#Mac/FixTdma set active_node_ 	16
#Mac/FixTdma set-table "tdma_table.txt"

#set net id 
Mac/DynamicTdma set assigned_Net_ 2		;# support net 1-8, default is net 1
Application/TdlDataApp set netID_ 2
set RxT_ 1e-13 ;#Receiving Threshold which mostly is a hardware feature
set Frequency_  [expr (300e+6) + 2500*2] ;# Signal Frequency which is also hardware feature

Phy/WirelessPhy set CPThresh_ 10.0
Phy/WirelessPhy set CSThresh_ 1e-13
Phy/WirelessPhy set RXThresh_ $RxT_ ;# Receiving Threshold in W
Phy/WirelessPhy set freq_ $Frequency_
Phy/WirelessPhy set L_ 1.0
Antenna/OmniAntenna set Z_ 100.0 ;# Antenna Height in meters

set val(Pt)             300000.0 ;# Transmission Power/Range in meters
set val(AnH)		100.0 ;# Antenna Height in meters

if { $val(prop) == "Propagation/TwoRayGround" } {
    set SL_ 300000000.0 ;# Speed of Light
    set PI 3.1415926535 ;# pi value
    set lambda [expr $SL_/$Frequency_]   ;# wavelength
    set lambda_2 [expr $lambda*$lambda]  ;# lambda^2
    set CoD_ [expr 4.0*$PI*$val(AnH)*$val(AnH)/$lambda] ;# Cross Over Distance
    puts "CoD is $CoD_"
    if { $val(Pt) <= $CoD_ } {;#Free Space for short distance communication
	set temp [expr 4.0*$PI*$val(Pt)]
	set TP_ [expr $RxT_*$temp*$temp/$lambda_2]
	Phy/WirelessPhy set Pt_ $TP_ ;#Set the Transmissiont Power w.r.t Distance
    } else { ;# TwoRayGround for communicating with far nodes
	set d4 [expr $val(Pt)*$val(Pt)*$val(Pt)*$val(Pt)]
	set hr2ht2 [expr $val(AnH)*$val(AnH)*$val(AnH)*$val(AnH)]
	set TP_  [expr $d4*$RxT_/$hr2ht2]
	Phy/WirelessPhy set Pt_ $TP_  ;#Set the Transmissiont Power w.r.t Distance
    }
}
#Phy/WirelessPhy set Pt_ 5000000.0
puts "tx power is $TP_"

#Create simulator object
set ns_ [new Simulator]
#Create file pointer object for write
set nf1 [open sim2.tr w]
set nf2 [open sim2.nam w]
set f1	[open out1.tr w]	;#packet arrival time trace
set f2 	[open out2.tr w]	;#net entry time as number of nodes in the net trace
set f3  [open out3.tr w]
set f4  [open out4.tr w]
set f5  [open out5.tr w]
set f6  [open out6.tr w]
set f7  [open out7.tr w]
set f8 	[open out8.tr w]
set f9	[open out9.tr w]
set f10 [open out10.tr w]
set f11 [open out11.tr w]
set f12	[open out12.tr w]
#Tell where trace should be written
$ns_ trace-all $nf1
$ns_ namtrace-all-wireless $nf2 $val(x) $val(y)

#define procedure to be executed after simulation is finished
proc stop {} {
	global ns_ nf1 nf2 f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12
        $ns_ flush-trace
        close $nf1
        close $nf2
        close $f1
        close $f2
        close $f3
        close $f4
        close $f5
        close $f6
        close $f7
        close $f8
        close $f9
        close $f10
        close $f11
        close $f12
}

#record procedure
proc recordPktArrTime {pkt_Id arr_time} {
	global f1
	puts $f1 "$arr_time $pkt_Id"
}
proc recordNETime {node_id num_node ne_time} {
	global f2
	puts $f2 "$node_id $num_node $ne_time"
}
proc recordNLTime {node_id leaving_node nl_time} {
	global f3
	puts $f3 "$node_id $leaving_node $nl_time"
}
proc recordResolveTime {node_id rec_type num_conflict frame_time} {
	global f4
	if { $rec_type == 1 } {
		puts $f4 "$node_id found-conflict $num_conflict $frame_time"
	} else {
		puts $f4 "$node_id resolve-conflict $num_conflict $frame_time"	
	}
}
proc recordMsgUpdateTime {node_id packet_id update_time} {
	global f5
	puts $f5 "$node_id $packet_id $update_time"
}
proc recordPacketDelay {node_id packet_id delay_time} {
	global f6
	puts $f6 "$node_id $packet_id $delay_time"
}
proc recordAvgPacketDelay {node_id record_time delay_time} {
	global f7
	puts $f7 "$node_id $record_time $delay_time"
}
proc recordThroughput {node_id record_time packets_tp bytes_tp tbytes_tp} {
	global f8
	puts $f8 "$node_id $record_time $packets_tp $bytes_tp $tbytes_tp"
}
proc recordRAP {node_id record_time src_id msg_seq bytes_rec} {
	global f9
	puts $f9 "$node_id $record_time $src_id $msg_seq $bytes_rec"
}
proc recordPosReport {node_id record_time src_id msg_seq bytes_rec} {
	global f10
	puts $f10 "$node_id $record_time $src_id $msg_seq $bytes_rec"
}
proc recordRadarTrack {node_id record_time src_id msg_seq bytes_rec} {
	global f11
	puts $f11 "$node_id $record_time $src_id $msg_seq $bytes_rec"
}
proc recordSlotUtil {node_id record_time slots_reserved slots_used} {
	global f12
	puts $f12 "$node_id $record_time $slots_reserved $slots_used"
}
#Setup topology
#define topology
set topo [new Topography]
$topo load_flatgrid $val(x) $val(y)
set god_ [create-god $val(nn)]

#configure node
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
                 -routerTrace OFF \
                 -macTrace OFF \
		 -movementTrace ON 
set active_node_ 0
for {set i 0} {$i < $val(nn) } {incr i} {
 set node_($i) [$ns_ node]
 $node_($i) random-motion 0 ;#disable random motion
 incr active_node_
}

# define node location
$node_(0) set X_ 1000.0

$node_(0) set Y_ 1000.0

$node_(0) set Z_ 0.0



$node_(1) set X_ 501000.0

$node_(1) set Y_ 501000.0

$node_(1) set Z_ 0.0



$node_(2) set X_ 1000.0
 
$node_(2) set Y_ 1001000.0
 
$node_(2) set Z_ 0.0


$node_(3) set X_ 1000.0

$node_(3) set Y_ 1700.0

$node_(3) set Z_ 0.0


$node_(4) set X_ 1200.0

$node_(4) set Y_ 1800.0

$node_(4) set Z_ 0.0


$node_(5) set X_ 1300.0

$node_(5) set Y_ 1001300.0

$node_(5) set Z_ 0.0


$node_(6) set X_ 1350.0

$node_(6) set Y_ 1001450.0

$node_(6) set Z_ 0.0




Application/TdlDataApp set pktsize_ 5000
Application/TdlDataApp set msgType_ 1

for {set i 0} {$i < 3 } {incr i} {
	#create transport agent
	set tdludp_($i) [new Agent/UDP/TdlDataUDP]
	$ns_ attach-agent $node_($i) $tdludp_($i)
	#create transport sink
	set sink_($i) [new Agent/UDP/TdlDataUDP]
	$ns_ attach-agent $node_($i) $sink_($i)
	#create application agent
	set tdldata_($i) [new Application/TdlDataApp]
	$tdldata_($i) attach-agent $tdludp_($i)
	#create application sink
	set sinkdata_($i) [new Application/TdlDataApp]
	$sinkdata_($i) attach-agent $sink_($i)
}
Application/TdlDataApp set pktsize_ 50
Application/TdlDataApp set msgType_ 3
for {set i 3} {$i < $val(nn) } {incr i} {
	#create transport agent
	set tdludp_($i) [new Agent/UDP/TdlDataUDP]
	$ns_ attach-agent $node_($i) $tdludp_($i)
	#create transport sink
	set sink_($i) [new Agent/UDP/TdlDataUDP]
	$ns_ attach-agent $node_($i) $sink_($i)
	#create application agent
	set tdldata_($i) [new Application/TdlDataApp]
	$tdldata_($i) attach-agent $tdludp_($i)
	#create application sink
	set sinkdata_($i) [new Application/TdlDataApp]
	$sinkdata_($i) attach-agent $sink_($i)
}




#connect transport agent
for {set i 0} {$i < $val(nn) } {incr i} {
	for {set j 0} {$j < $val(nn) } {incr j} {
		if {$i != $j} {
			$ns_ connect $tdludp_($i) $sink_($j)
		}
	}
}







#

# Tell nodes when the simulation ends

#


#for {set i 0} {$i < $active_node_ } {incr i} {

#    $ns_ at 500.1 "$node_($i) reset";

#}

$ns_ at 1.0 "$tdldata_(0) start"
$ns_ at 1.0 "$tdldata_(1) start"
$ns_ at 1.0 "$tdldata_(2) start"
$ns_ at 16.0 "$tdldata_(3) start"
$ns_ at 26.0 "$tdldata_(4) start"
$ns_ at 36.0 "$tdldata_(5) start"
$ns_ at 46.0 "$tdldata_(6) start"
#create some movement
$ns_ at 60.0 "$node_(5) setdest 500200.0 500300.0 550.0"
$ns_ at 62.0 "$node_(6) setdest 500201.0 500440.0 550.0"
$ns_ at 65.0 "$node_(3) setdest 500350.0 500500.0 550.0"
$ns_ at 67.0 "$node_(4) setdest 500500.0 500600.0 550.0"


$ns_ at 1000.0 "$node_(3) setdest 2000.0 2300.0 550.0"
$ns_ at 1000.0 "$node_(4) setdest 902001.0 500440.0 550.0"
$ns_ at 1000.0 "$node_(5) setdest 901635.0 500500.0 550.0"
$ns_ at 1000.0 "$node_(6) setdest 902550.0 500600.0 550.0"

#$ns_ at 1500.0 "$node_(3) setdest 2000.0 2300.0 550.0"
#$ns_ at 1500.0 "$node_(4) setdest 902001.0 500440.0 550.0"
#$ns_ at 1500.0 "$node_(5) setdest 901635.0 500500.0 550.0"
#$ns_ at 1500.0 "$node_(6) setdest 902550.0 500600.0 550.0"




#change message
$ns_ at 1640.0 "$tdldata_(5) change-message 2 1000"
$ns_ at 1650.0 "$tdldata_(6) change-message 2 1000"


$ns_ at 3000.5 "puts \"stop...\" ;"
$ns_ at 3000.5 "stop"
$ns_ at 3000.5 "puts \"NS EXITING...\" ; $ns_ halt"


$ns_ run
