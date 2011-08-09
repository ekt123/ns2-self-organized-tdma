#Simulation Testing: initialization and net entry phase
#
#Simulation Setup
# 16 mobile nodes, start 1 node at a time and measure net entry time  
# All nodes transmit position report message


#remove unnescessary headers 
remove-all-packet-headers
#add require header i.e. IP
add-packet-header IP RTP ARP LL Mac HdrNbInfo TdlData NetEntryMsg PollingMsg

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

set val(nn)             16                         ;# number of mobilenodes

set val(rp)             NOAH                       ;# routing protocol

set val(x)		5000			   ;# X dimension of topology

set val(y)		5000			   ;# Y dimension of topology

#set channel bandwidth
Mac set bandwidth_ 5e4
#Mac/FixTdma set slot_packet_len_ 300
#Mac/FixTdma set max_slot_num_	200
#Mac/FixTdma set active_node_ 	16
#Mac/FixTdma set-table "tdma_table.txt"

#set net id 
Mac/DynamicTdma set assigned_Net_ 2		;# support net 1-8, default is net 1
set RxT_ 2e-15 ;#Receiving Threshold which mostly is a hardware feature
set Frequency_  300e+6 ;# Signal Frequency which is also hardware feature

Phy/WirelessPhy set CPThresh_ 10.0
Phy/WirelessPhy set CSThresh_ 2e-15
Phy/WirelessPhy set RXThresh_ $RxT_ ;# Receiving Threshold in W
Phy/WirelessPhy set freq_ $Frequency_
Phy/WirelessPhy set L_ 1.0

set val(Pt)             300000.0 ;# Transmission Power/Range in meters
set val(AnH)		1000.0 ;# Antenna Height in meters
if { $val(prop) == "Propagation/TwoRayGround" } {
    set SL_ 300000000.0 ;# Speed of Light
    set PI 3.1415926535 ;# pi value
    set lambda [expr $SL_/$Frequency_]   ;# wavelength
    set lambda_2 [expr $lambda*$lambda]  ;# lambda^2
    set CoD_ [expr 4.0*$PI*$val(AnH)*$val(AnH)/$lambda] ;# Cross Over Distance
    
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

puts "tx power is $TP_"

#Create simulator object
set ns_ [new Simulator]
#Create file pointer object for write
set nf1 [open sim2.tr w]
set nf2 [open sim2.nam w]
set f1	[open out1.tr w]	;#packet arrival time trace
set f2 	[open out2.tr w]	;#net entry time as number of nodes in the net trace

#Tell where trace should be written
$ns_ trace-all $nf1
$ns_ namtrace-all-wireless $nf2 $val(x) $val(y)

#define procedure to be executed after simulation is finished
proc stop {} {
	global ns_ nf1 nf2 f1 f2
        $ns_ flush-trace
        close $nf1
        close $nf2
        close $f1
        close $f2
        #puts "running nam..."
        #exec nam sim1.nam &
        #exit 0
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

#Setup topology
#define topology
set topo [new Topography]

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
                 -macTrace ON 
set active_node_ 0
for {set i 0} {$i < $val(nn) } {incr i} {
 set node_($i) [$ns_ node]
 $node_($i) random-motion 0 ;#disable random motion
 incr active_node_
}

# define node location
$node_(0) set X_ 100.0

$node_(0) set Y_ 100.0

$node_(0) set Z_ 0.0



$node_(1) set X_ 300.0

$node_(1) set Y_ 300.0

$node_(1) set Z_ 0.0



$node_(2) set X_ 200.0
 
$node_(2) set Y_ 150.0
 
$node_(2) set Z_ 0.0
# 
# 
# 
$node_(3) set X_ 200.0

$node_(3) set Y_ 250.0

$node_(3) set Z_ 0.0


$node_(4) set X_ 500.0

$node_(4) set Y_ 650.0

$node_(4) set Z_ 0.0


$node_(5) set X_ 700.0

$node_(5) set Y_ 700.0

$node_(5) set Z_ 0.0



$node_(6) set X_ 800.0

$node_(6) set Y_ 300.0

$node_(6) set Z_ 0.0



$node_(7) set X_ 800.0
 
$node_(7) set Y_ 150.0
 
$node_(7) set Z_ 0.0
# 
# 
# 
$node_(8) set X_ 700.0

$node_(8) set Y_ 250.0

$node_(8) set Z_ 0.0


$node_(9) set X_ 600.0

$node_(9) set Y_ 800.0

$node_(9) set Z_ 0.0


$node_(10) set X_ 50.0

$node_(10) set Y_ 800.0

$node_(10) set Z_ 0.0


$node_(11) set X_ 1000.0

$node_(11) set Y_ 200.0

$node_(11) set Z_ 0.0


$node_(12) set X_ 2500.0

$node_(12) set Y_ 2000.0
 
$node_(12) set Z_ 0.0
 
 
$node_(13) set X_ 10.0
 
$node_(13) set Y_ 20.0
 
$node_(13) set Z_ 0.0
 

$node_(14) set X_ 300.0
 
$node_(14) set Y_ 800.0
 
$node_(14) set Z_ 0.0
 
 
$node_(15) set X_ 100.0
 
$node_(15) set Y_ 20.0
 
$node_(15) set Z_ 0.0

#create some movement
#$ns_ at 10.0 "$node_(0) setdest 150.0 150.0 10.0"


for {set i 0} {$i < $val(nn) } {incr i} {
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


# #create sinks
# set sink0 [new Agent/UDP/TdlDataUDP]
# set sink1 [new Agent/UDP/TdlDataUDP]
# set sink2 [new Agent/UDP/TdlDataUDP]
# set sink3 [new Agent/UDP/TdlDataUDP]
# set sink4 [new Agent/UDP/TdlDataUDP]
# $ns_ attach-agent $node_(0) $sink0
# #$ns attach-agent $n1 $sink1
# $ns_ attach-agent $node_(1) $sink1
# $ns_ attach-agent $node_(2) $sink2
# $ns_ attach-agent $node_(3) $sink3

#create application agent
# set tdldata0 [new Application/TdlDataApp]
# $tdldata0 attach-agent $tdludp0
# set sinkdata0 [new Application/TdlDataApp]
# $sinkdata0 attach-agent $sink0
# set tdldata1 [new Application/TdlDataApp]
# $tdldata1 attach-agent $tdludp1
# set sinkdata1 [new Application/TdlDataApp]
# $sinkdata1 attach-agent $sink1
# set tdldata2 [new Application/TdlDataApp]
# $tdldata2 attach-agent $tdludp2
# set sinkdata2 [new Application/TdlDataApp]
# $sinkdata2 attach-agent $sink2
# set tdldata3 [new Application/TdlDataApp]
# $tdldata3 attach-agent $tdludp3
# set sinkdata3 [new Application/TdlDataApp]
# $sinkdata3 attach-agent $sink3

#classified packet
# $tdldata0 set class_ 1
# $tdldata1 set class_ 2
# 
# $ns_ color 1 Blue
# $ns_ color 2 Red

#connect transport agent
for {set i 0} {$i < $val(nn) } {incr i} {
	for {set j 0} {$j < $val(nn) } {incr j} {
		if {$i != $j} {
			$ns_ connect $tdludp_($i) $sink_($j)
		}
	}
}
# $ns_ connect $tdludp0 $sink1
# $ns_ connect $tdludp0 $sink2
# $ns_ connect $tdludp0 $sink3
# 
# $ns_ connect $tdludp1 $sink0
# $ns_ connect $tdludp1 $sink2
# $ns_ connect $tdludp1 $sink3
# 
# $ns_ connect $tdludp2 $sink0
# $ns_ connect $tdludp2 $sink1
# $ns_ connect $tdludp2 $sink3
# 
# $ns_ connect $tdludp3 $sink0
# $ns_ connect $tdludp3 $sink1
# $ns_ connect $tdludp3 $sink2






#

# Tell nodes when the simulation ends

#


for {set i 0} {$i < $active_node_ } {incr i} {

    $ns_ at 300.1 "$node_($i) reset";

}

$ns_ at 1.0 "$tdldata_(0) start"
$ns_ at 5.0 "$tdldata_(1) start"
$ns_ at 10.0 "$tdldata_(2) start"
$ns_ at 15.0 "$tdldata_(3) start"
$ns_ at 20.0 "$tdldata_(4) start"
$ns_ at 25.0 "$tdldata_(5) start"
$ns_ at 30.0 "$tdldata_(6) start"
$ns_ at 35.0 "$tdldata_(7) start"
$ns_ at 40.0 "$tdldata_(8) start"
$ns_ at 45.0 "$tdldata_(9) start"
$ns_ at 50.0 "$tdldata_(10) start"
$ns_ at 55.0 "$tdldata_(11) start"
$ns_ at 60.0 "$tdldata_(12) start"
$ns_ at 65.0 "$tdldata_(13) start"
$ns_ at 70.0 "$tdldata_(14) start"
$ns_ at 75.0 "$tdldata_(15) start"
#$ns_ at 10.0 "$tdldata0 change-message 1 1 100 5.0"
# $ns_ at 298.9 "puts \"write to file\" ;"
# $ns_ at 299.0 "close $f2"
# $ns_ at 299.0 "$tdldata_(0) stop"
# $ns_ at 299.0 "$tdldata_(1) stop"
# $ns_ at 299.0 "$tdldata_(2) stop"
# $ns_ at 299.0 "$tdldata_(3) stop"
# $ns_ at 299.0 "$tdldata_(4) stop"
# $ns_ at 299.0 "$tdldata_(5) stop"
# $ns_ at 299.0 "$tdldata_(6) stop"
# $ns_ at 299.0 "$tdldata_(7) stop"
# $ns_ at 299.0 "$tdldata_(8) stop"
# $ns_ at 299.0 "$tdldata_(9) stop"
# $ns_ at 299.0 "$tdldata_(10) stop"
# $ns_ at 299.0 "$tdldata_(11) stop"
# $ns_ at 299.0 "$tdldata_(12) stop"
# $ns_ at 299.0 "$tdldata_(13) stop"
# $ns_ at 300.0 "$tdldata_(14) stop"
# $ns_ at 300.0 "$tdldata_(15) stop"
$ns_ at 300.5 "puts \"stop...\" ;"
$ns_ at 300.5 "stop"
$ns_ at 300.5 "puts \"NS EXITING...\" ; $ns_ halt"


$ns_ run



