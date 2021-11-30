set val(chan)        Channel/WirelessChannel    ;#Channel Type
set val(prop)        Propagation/TwoRayGround   ;# radio-propagation model
set val(netif)       Phy/WirelessPhy            ;# network interface type
set val(mac)         Mac/Simple                 ;# MAC type
set val(ifq)         Queue/DropTail/PriQueue    ;# interface queue type
set val(ll)          LL                         ;# link layer type
set val(ant)         Antenna/OmniAntenna        ;# antenna model
set val(ifqlen)      50                         ;# max packet in ifq
set val(nn)          50                         ;# number of mobilenodes
set val(rp)          AODV                    
set val(x)        1500
set val(y)        300

 set opt(stop) 9

# Initialize Global Variables
set ns_            [new Simulator]
set tracef     [open nLab.tr w]
$ns_ trace-all $tracef

set namtrace [open mm.nam w]
$ns_  namtrace-all-wireless $namtrace $val(x) $val(y)

# set up topography object
set topo       [new Topography]
$topo load_flatgrid $val(x) $val(y)

# Create God
set god_ [create-god $val(nn)]

# Create channel
set chan_ [new $val(chan)]

# Create node(0) and node(1)

# configure node, please note the change below.

$ns_ node-config -adhocRouting $val(rp) \
            -llType $val(ll) \
            -macType $val(mac) \
            -ifqType $val(ifq) \
            -ifqLen $val(ifqlen) \
            -antType $val(ant) \
            -propType $val(prop) \
            -phyType $val(netif) \
            -topoInstance $topo \
            -energyModel "EnergyModel" \
            -initialEnergy 2.0 \
            -txPower 0.4 \
            -rxPower 0.1 \
            -agentTrace ON \
            -routerTrace ON \
            -macTrace ON \
            -movementTrace ON \
            -channel $chan_

 

for {set i 0} {$i < $val(nn)} {incr i} {
    set node_($i) [$ns_ node]
    $node_($i) random-motion 0
}

#
# Multicast group
# 1 sender: node 0
# receiver(s): nodes 40 through 49
#
set udp_(0) [new Agent/UDP]
$udp_(0) set dst_addr_ 0xE000000
$ns_ attach-agent $node_(0) $udp_(0)
#
set cbr_(0) [new Application/Traffic/CBR]
$cbr_(0) set packetSize_ 256
$cbr_(0) set interval_ 0.50
$cbr_(0) set random_ 1
# send enough packets to keep simulation nearly busy: 2 packets
# a second, starting at 30, stopping at 899: 2*870 = 1740
$cbr_(0) set maxpkts_ 1740
$cbr_(0) attach-agent $udp_(0)
$cbr_(0) set dst_ 0xE000000
$ns_ at 30.0 "$cbr_(0) start"
#
# the nodes have to join the multicast group to receive the packet...
#
# for {set i 40} {$i < 50} {incr i} {
# 	$ns_ at 0.0100000000 "$node_($i) aodv-join-group 0xE000000"
# }

source "scenarios/scen-1500x300-50-0-1-1"


for {set i 0} {$i < $val(nn)} {incr i} {
	$ns_ at $opt(stop) "$node_($i) reset";
}

$ns_ at $opt(stop) "$ns_ halt"

puts "Starting Simulation ..."
$ns_ run
