set NUMBER_OF_NODES 50
set TIME 50
# Node speed is from energy.txt file
set INITIAL_ENERGY_NODE 2
set txPower 0.4
set rxPower 0.1
set RECEIVING_NODES 10


set val(chan)        Channel/WirelessChannel    ;#Channel Type
set val(prop)        Propagation/TwoRayGround   ;# radio-propagation model
set val(netif)       Phy/WirelessPhy            ;# network interface type
set val(mac)         Mac/Simple                 ;# MAC type
set val(ifq)         Queue/DropTail/PriQueue    ;# interface queue type
set val(ll)          LL                         ;# link layer type
set val(ant)         Antenna/OmniAntenna        ;# antenna model
set val(ifqlen)      50                         ;# max packet in ifq
set val(nn)          $NUMBER_OF_NODES           ;# number of mobilenodes
set val(rp)          AODV                    
set val(x)        1500
set val(y)        300
set opt(stop) 9

# Initialize Global Variables
set ns            [new Simulator]
set tracef     [open trace-$val(nn).tr w]
$ns trace-all $tracef
 
set namf [open nam-$val(nn).nam w]
$ns namtrace-all-wireless $namf $val(x) $val(y)
 
# set up topography object
set topo       [new Topography]
 
$topo load_flatgrid $val(x) $val(y)
 
# Create God
set god_ [create-god $val(nn)]
 
# Create channel
set chan_ [new $val(chan)]
 
# Create node(0) and node(1)
 
# configure node, please note the change below.
$ns node-config -adhocRouting $val(rp) \
            -llType $val(ll) \
            -macType $val(mac) \
            -ifqType $val(ifq) \
            -ifqLen $val(ifqlen) \
            -antType $val(ant) \
            -propType $val(prop) \
            -phyType $val(netif) \
            -topoInstance $topo \
            -energyModel "EnergyModel" \
            -initialEnergy $INITIAL_ENERGY_NODE \
            -txPower $txPower \
            -rxPower $rxPower \
            -agentTrace ON \
            -routerTrace ON \
            -macTrace ON \
            -movementTrace ON \
            -channel $chan_
 
for {set i 0} {$i < $val(nn)} {incr i} {
    set node_($i) [$ns node]
    $node_($i) random-motion 0
}
source "scenarios/scen-1500x300-50-0-1-1"

for {set i 0} {$i < $RECEIVING_NODES} {incr i} {
    set tcp [new Agent/TCP]
    $tcp set class_ 2
    set sink [new Agent/TCPSink]
    $ns attach-agent $node_($i) $tcp
    set sink_addr [expr 49- $i]
    puts "Sink address: $sink_addr"
    $ns attach-agent $node_($sink_addr) $sink
    $ns connect $tcp $sink
    set ftp [new Application/FTP]
    $ftp attach-agent $tcp
    $ns at 0.1 "$ftp start"
}

for {set i 0} {$i < $val(nn) } {incr i} {
    $ns at 1 "$node_($i) reset";
}
$ns at 3.0 "stop"
$ns at 3.01 "$ns halt"
proc stop {} {
    global ns tracef
    $ns flush-trace
    close $tracef
}
 
puts "Starting Simulation..."
$ns run