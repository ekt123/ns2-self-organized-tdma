#First Simulation: tdl message
#
#Simulation Setup
# 2 stationary nodes 
# simple link
# tdl message

#remove unnescessary headers 
remove-all-packet-headers
#add require header i.e. IP
add-packet-header IP RTP ARP TDL_DATA

#Create simulator object
set ns [new Simulator]
#Create file pointer object for write
set nf1 [open sim1.tr w]
set nf2 [open sim1.nam w]

#Tell where trace should be written
$ns trace-all $nf1
$ns namtrace-all $nf2

#define procedure to be executed after simulation is finished
proc finish {} {
	global ns nf1 nf2
        $ns flush-trace
        close $nf1
        close $nf2
        
        puts "running nam..."
        exec nam sim1.nam &
        exit 0
}

#Setup topology
#define nodes
set n0 [$ns node]
set n1 [$ns node]


#define connections
$ns duplex-link $n0 $n1 1Mb 2ms DropTail

#create transport agent
set tdludp0 [new Agent/UDP/TdlDataUDP]
$ns attach-agent $n0 $tdludp0
set tdludp1 [new Agent/UDP/TdlDataUDP]
$ns attach-agent $n1 $tdludp1




#create sinks
set sink0 [new Agent/UDP/TdlDataUDP]
set sink1 [new Agent/UDP/TdlDataUDP]
$ns attach-agent $n0 $sink0
#$ns attach-agent $n1 $sink1
$ns attach-agent $n1 $sink1

#create application agent
set tdldata0 [new Application/TdlDataApp]
$tdldata0 attach-agent $tdludp0
set tdldata1 [new Application/TdlDataApp]
$tdldata1 attach-agent $tdludp1


#classified packet
$tdldata0 set class_ 1
$tdldata1 set class_ 2

$ns color 1 Blue
$ns color 2 Red

#connect transport agent
#$ns connect $udp0 $sink1
$ns connect $tdludp0 $sink1
$ns connect $tdludp1 $sink0

$ns at 1.0 "$tdldata0 start"
$ns at 5.0 "$tdldata1 start"
$ns at 30.0 "$tdldata0 stop"
$ns at 40.0 "$tdldata1 stop"
$ns at 40.5 "finish"

$ns run



