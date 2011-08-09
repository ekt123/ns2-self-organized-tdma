#Second Simulation: tdl message over MAC/FixTDMA
#
#Simulation Setup
# 4 active mobile nodes 
# tdl message
# tdl modified udp (broadcasting)
# Droptail queue
# ip
# NOAH ad-hoc routing
# LL 
# Mac/FixTDMA
# WirelessPhy
# WirelessChannel
# Two-ray prog
# Omni antenna

#remove unnescessary headers 
remove-all-packet-headers
#add require header i.e. IP
add-packet-header IP RTP AODV ARP LL Mac TDL_DATA

# ======================================================================

# Define options

# ======================================================================




set val(chan)           Channel/WirelessChannel    ;# channel type

set val(prop)           Propagation/TwoRayGround   ;# radio-propagation model

set val(netif)          Phy/WirelessPhy            ;# network interface type

set val(mac)            Mac/FixTdma                ;# MAC type

set val(ifq)            Queue/DropTail/PriQueue    ;# interface queue type

set val(ll)             LL                         ;# link layer type

set val(ant)            Antenna/OmniAntenna        ;# antenna model

set val(ifqlen)         100                        ;# max packet in ifq

set val(nn)             16                         ;# number of mobilenodes

set val(rp)             NOAH                       ;# routing protocol

set val(x)		500			   ;# X dimension of topology

set val(y)		500			   ;# Y dimension of topology


Mac set bandwidth_ 5e4
Mac/FixTdma set slot_packet_len_ 300
Mac/FixTdma set max_slot_num_	200
Mac/FixTdma set active_node_ 	16
#Mac/FixTdma set-table "tdma_table.txt"

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

#Tell where trace should be written
$ns_ trace-all $nf1
$ns_ namtrace-all-wireless $nf2 $val(x) $val(y)

#define procedure to be executed after simulation is finished
proc stop {} {
	global ns_ nf1 nf2
        $ns_ flush-trace
        close $nf1
        close $nf2
        
        #puts "running nam..."
        #exec nam sim1.nam &
        #exit 0
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
                 -macTrace OFF 
set active_node_ 0
for {set i 0} {$i < 4 } {incr i} {
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



$node_(3) set X_ 200.0

$node_(3) set Y_ 250.0

$node_(3) set Z_ 0.0





#create some movement
#$ns_ at 10.0 "$node_(0) setdest 150.0 150.0 10.0"

#create transport agent
set tdludp0 [new Agent/UDP/TdlDataUDP]
$ns_ attach-agent $node_(0) $tdludp0
set tdludp1 [new Agent/UDP/TdlDataUDP]
$ns_ attach-agent $node_(1) $tdludp1
set tdludp2 [new Agent/UDP/TdlDataUDP]
$ns_ attach-agent $node_(2) $tdludp2
set tdludp3 [new Agent/UDP/TdlDataUDP]
$ns_ attach-agent $node_(3) $tdludp3
set tdludp4 [new Agent/UDP/TdlDataUDP]



#create sinks
set sink0 [new Agent/UDP/TdlDataUDP]
set sink1 [new Agent/UDP/TdlDataUDP]
set sink2 [new Agent/UDP/TdlDataUDP]
set sink3 [new Agent/UDP/TdlDataUDP]
set sink4 [new Agent/UDP/TdlDataUDP]
$ns_ attach-agent $node_(0) $sink0
#$ns attach-agent $n1 $sink1
$ns_ attach-agent $node_(1) $sink1
$ns_ attach-agent $node_(2) $sink2
$ns_ attach-agent $node_(3) $sink3

#create application agent
set tdldata0 [new Application/TdlDataApp]
$tdldata0 attach-agent $tdludp0
set sinkdata0 [new Application/TdlDataApp]
$sinkdata0 attach-agent $sink0
set tdldata1 [new Application/TdlDataApp]
$tdldata1 attach-agent $tdludp1
set sinkdata1 [new Application/TdlDataApp]
$sinkdata1 attach-agent $sink1
set tdldata2 [new Application/TdlDataApp]
$tdldata2 attach-agent $tdludp2
set sinkdata2 [new Application/TdlDataApp]
$sinkdata2 attach-agent $sink2
set tdldata3 [new Application/TdlDataApp]
$tdldata3 attach-agent $tdludp3
set sinkdata3 [new Application/TdlDataApp]
$sinkdata3 attach-agent $sink3

#classified packet
$tdldata0 set class_ 1
$tdldata1 set class_ 2

$ns_ color 1 Blue
$ns_ color 2 Red

#connect transport agent
#$ns connect $udp0 $sink1
$ns_ connect $tdludp0 $sink1
$ns_ connect $tdludp0 $sink2
$ns_ connect $tdludp0 $sink3

$ns_ connect $tdludp1 $sink0
$ns_ connect $tdludp1 $sink2
$ns_ connect $tdludp1 $sink3

$ns_ connect $tdludp2 $sink0
$ns_ connect $tdludp2 $sink1
$ns_ connect $tdludp2 $sink3

$ns_ connect $tdludp3 $sink0
$ns_ connect $tdludp3 $sink1
$ns_ connect $tdludp3 $sink2






#

# Tell nodes when the simulation ends

#


for {set i 0} {$i < $active_node_ } {incr i} {

    $ns_ at 50.0 "$node_($i) reset";

}

$ns_ at 1.0 "$tdldata0 start"
$ns_ at 5.0 "$tdldata1 start"
$ns_ at 10.0 "$tdldata0 change-message 1 1 100 5.0"
$ns_ at 50.0 "$tdldata0 stop"
$ns_ at 50.0 "$tdldata1 stop"
$ns_ at 50.0 "stop"
$ns_ at 50.1 "puts \"NS EXITING...\" ; $ns_ halt"


$ns_ run



