#Simulation Testing: initialization and net entry phase
#
#Simulation Setup
# Study scene 2: Air Offensive Scenario


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

set val(nn)             15                         ;# number of nodes (3 ground stations, 8 mobile nodes)

set val(rp)             NOAH                       ;# routing protocol

set val(x)		50000000			   ;# X dimension of topology

set val(y)		50000000			   ;# Y dimension of topology

set val(ni)		1
#set channel bandwidth
Mac set bandwidth_ 5e4


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
#set nf2 [open sim2.nam w]
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
#$ns_ namtrace-all-wireless $nf2 $val(x) $val(y)

#define procedure to be executed after simulation is finished
proc stop {} {
	global ns_ nf1 f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12
        $ns_ flush-trace
        close $nf1
        #close $nf2
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
		 -numif $val(ni) \
		 -agentTrace OFF \
                 -routerTrace OFF \
                 -macTrace OFF \
		 -movementTrace OFF 
set active_node_ 0
Mac/DynamicTdma set is_active_ 0
for {set i 0} {$i < 3 } {incr i} {
 set node_($i) [$ns_ node]
 $node_($i) random-motion 0 ;#disable random motion
}
Mac/DynamicTdma set is_active_ 1
for {set i 3} {$i < $val(nn) } {incr i} {
 set node_($i) [$ns_ node]
 $node_($i) random-motion 0 ;#disable random motion
 incr active_node_
}

# define node location
# 3 ground stations, not active in this scenario

$node_(0) set X_ 100.0

$node_(0) set Y_ 100.0

$node_(0) set Z_ 0.0



$node_(1) set X_ 501000.0

$node_(1) set Y_ 501000.0

$node_(1) set Z_ 0.0



$node_(2) set X_ 1000.0
 
$node_(2) set Y_ 1001000.0
 
$node_(2) set Z_ 0.0

# 8 mobile nodes
# 4 bases about 500km away from each other 
# 3 mobile nodes (68,69,70) are in base 1
$node_(3) set X_ 100.0

$node_(3) set Y_ 200.0

$node_(3) set Z_ 0.0


$node_(4) set X_ 100.0

$node_(4) set Y_ 400.0

$node_(4) set Z_ 0.0


$node_(5) set X_ 100.0

$node_(5) set Y_ 600.0

$node_(5) set Z_ 0.0

# 3 mobile nodes (71,72,73) are in base 2
$node_(6) set X_ 100.0

$node_(6) set Y_ 500200.0

$node_(6) set Z_ 0.0

$node_(7) set X_ 100.0

$node_(7) set Y_ 500400.0

$node_(7) set Z_ 0.0


$node_(8) set X_ 100.0

$node_(8) set Y_ 500600.0

$node_(8) set Z_ 0.0

# 3 mobile nodes (74,75,76) are in base 3
$node_(9) set X_ 100.0

$node_(9) set Y_ 1000200.0

$node_(9) set Z_ 0.0


$node_(10) set X_ 100.0

$node_(10) set Y_ 1000400.0

$node_(10) set Z_ 0.0


$node_(11) set X_ 100.0

$node_(11) set Y_ 1000600.0

$node_(11) set Z_ 0.0

# 3 mobile nodes (77,78,79) are in base 4
$node_(12) set X_ 100.0

$node_(12) set Y_ 1500200.0

$node_(12) set Z_ 0.0


$node_(13) set X_ 100.0

$node_(13) set Y_ 1500400.0

$node_(13) set Z_ 0.0


$node_(14) set X_ 100.0

$node_(14) set Y_ 1500600.0

$node_(14) set Z_ 0.0

# Set up Tdl agent to active node
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
for {set i 3} {$i < $val(nn) } {incr i} {
	for {set j 3} {$j < $val(nn) } {incr j} {
		if {$i != $j} {
			$ns_ connect $tdludp_($i) $sink_($j)
		}
	}
}










$ns_ at 1.0 "$tdldata_(3) start"
$ns_ at 8.0 "$tdldata_(4) start"
$ns_ at 15.0 "$tdldata_(5) start"
$ns_ at 1.0 "$tdldata_(6) start"
$ns_ at 8.0 "$tdldata_(7) start"
$ns_ at 15.0 "$tdldata_(8) start"
$ns_ at 1.0 "$tdldata_(9) start"
$ns_ at 8.0 "$tdldata_(10) start"
$ns_ at 15.0 "$tdldata_(11) start"
$ns_ at 1.0 "$tdldata_(12) start"
$ns_ at 8.0 "$tdldata_(13) start"
$ns_ at 15.0 "$tdldata_(14) start"
#create some movement
#all a/c move to entry point
$ns_ at 60.0 "$node_(3) setdest 550000.0 550100.0 550.0"
$ns_ at 80.0 "$node_(4) setdest 550000.0 550200.0 550.0"
$ns_ at 90.0 "$node_(5) setdest 550000.0 550300.0 550.0"
$ns_ at 60.0 "$node_(6) setdest 550000.0 550400.0 550.0"
$ns_ at 80.0 "$node_(7) setdest 550000.0 550500.0 550.0"
$ns_ at 90.0 "$node_(8) setdest 550000.0 550600.0 550.0"
$ns_ at 60.0 "$node_(9) setdest 550000.0 550700.0 550.0"
$ns_ at 80.0 "$node_(10) setdest 550000.0 550800.0 550.0"
$ns_ at 90.0 "$node_(11) setdest 550000.0 550900.0 550.0"
$ns_ at 60.0 "$node_(12) setdest 550000.0 551000.0 550.0"
$ns_ at 80.0 "$node_(13) setdest 550000.0 551100.0 550.0"
$ns_ at 90.0 "$node_(14) setdest 550000.0 551200.0 550.0"

#all a/c move into enemy zone
$ns_ at 1050.0 "$node_(3) setdest 12500000.0 550100.0 550.0"
$ns_ at 1050.0 "$node_(4) setdest 12500000.0 550200.0 550.0"
$ns_ at 1050.0 "$node_(5) setdest 12500000.0 550300.0 550.0"
$ns_ at 1050.0 "$node_(6) setdest 12500000.0 550400.0 550.0"
$ns_ at 1050.0 "$node_(7) setdest 12500000.0 550500.0 550.0"
$ns_ at 1050.0 "$node_(8) setdest 12500000.0 550600.0 550.0"
$ns_ at 1050.0 "$node_(9) setdest 12500000.0 550700.0 550.0"
$ns_ at 1050.0 "$node_(10) setdest 12500000.0 550800.0 550.0"
$ns_ at 1050.0 "$node_(11) setdest 12500000.0 550900.0 550.0"
$ns_ at 1050.0 "$node_(12) setdest 12500000.0 551000.0 550.0"
$ns_ at 1050.0 "$node_(13) setdest 12500000.0 551100.0 550.0"
$ns_ at 1050.0 "$node_(14) setdest 12500000.0 551200.0 550.0"


#change message
#$ns_ at 1647.0 "$tdldata_(11) change-message 2 1000"
# throughput slope should increase significantly
$ns_ at 1155.0 "$tdldata_(3) change-message 2 400"
$ns_ at 1175.0 "$tdldata_(4) change-message 2 400"
$ns_ at 1186.0 "$tdldata_(5) change-message 2 400"
$ns_ at 1195.0 "$tdldata_(6) change-message 2 400"
$ns_ at 1216.0 "$tdldata_(7) change-message 2 400"
$ns_ at 1236.0 "$tdldata_(8) change-message 2 400"
$ns_ at 1244.0 "$tdldata_(9) change-message 2 400"
$ns_ at 1255.0 "$tdldata_(10) change-message 2 400"
# throughput slope should increase. even though node 10-13 can only reserve one 
# slot but the size change from 50 to 200, one slot can hold a 200-byte track message
# Hence, update rate still be satisfied at this point
#$ns_ at 4395.0 "$tdldata_(10) change-message 2 400"
#$ns_ at 4476.0 "$tdldata_(11) change-message 2 400"
#$ns_ at 4556.0 "$tdldata_(12) change-message 2 400"
#$ns_ at 4664.0 "$tdldata_(13) change-message 2 400"
# throughput slope should increase and update rate should be satisfied
$ns_ at 1555.0 "$tdldata_(3) change-message 2 800"
$ns_ at 1575.0 "$tdldata_(4) change-message 2 800"
$ns_ at 1586.0 "$tdldata_(5) change-message 2 800"
$ns_ at 1595.0 "$tdldata_(6) change-message 2 800"
$ns_ at 1626.0 "$tdldata_(7) change-message 2 800"
#$ns_ at 1636.0 "$tdldata_(8) change-message 2 800"
#$ns_ at 1644.0 "$tdldata_(9) change-message 2 800"
# throughput slope should not increase and update rate for node 10-13 should not
# be satisfied because the size of track message is more than the size of one time
# slot and node 10-13 only reserve one time slot in required period
#$ns_ at 7055.0 "$tdldata_(10) change-message 2 400"
#$ns_ at 7175.0 "$tdldata_(11) change-message 2 400"
$ns_ at 2086.0 "$tdldata_(8) change-message 2 800"
$ns_ at 2095.0 "$tdldata_(9) change-message 2 800"
$ns_ at 2106.0 "$tdldata_(10) change-message 2 800"
# throughput slope should increase and update rate should be satisfied
$ns_ at 2455.0 "$tdldata_(3) change-message 2 1000"
$ns_ at 2475.0 "$tdldata_(4) change-message 2 1000"
$ns_ at 2486.0 "$tdldata_(5) change-message 2 1000"
$ns_ at 2535.0 "$tdldata_(6) change-message 2 1000"
$ns_ at 2576.0 "$tdldata_(7) change-message 2 1000"
#$ns_ at 2588.0 "$tdldata_(8) change-message 2 1000"
#$ns_ at 2594.0 "$tdldata_(9) change-message 2 1000"
# throughput slope should not increase and update rate should not be satisfied
$ns_ at 3013.0 "$tdldata_(3) change-message 2 1200"
$ns_ at 3025.0 "$tdldata_(4) change-message 2 1200"
$ns_ at 3064.0 "$tdldata_(5) change-message 2 1200"
$ns_ at 3085.0 "$tdldata_(6) change-message 2 1200"
#$ns_ at 32940.0 "$tdldata_(7) change-message 2 1500"
#$ns_ at 32995.0 "$tdldata_(8) change-message 2 1500"

$ns_ at 3413.0 "$tdldata_(3) change-message 2 1500"
$ns_ at 3425.0 "$tdldata_(4) change-message 2 1500"
$ns_ at 3464.0 "$tdldata_(5) change-message 2 1500"
$ns_ at 3485.0 "$tdldata_(6) change-message 2 1500"

$ns_ at 4000.0 "puts \"stop...\" ;"
$ns_ at 4000.0 "stop"
$ns_ at 4000.0 "puts \"NS EXITING...\" ; $ns_ halt"


$ns_ run
