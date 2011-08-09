# Special case of Agent for distributed ns simulations
# George F. Riley.  Georgia Tech, Fall 1998

# Agents on distributed ns simulations can connect to agents on 
# other systems, so we need some way to identify what system and
# what agent is desired.  The dst-ipaddr and dst-port manage this.
#
# During a "rconnect" ns command, only the local values are set, using
# the commands below.  They are filled in to specify dst_ as the correct
# border router during "$ns run" initialization

Agent instproc set-dst-ipaddr dstipaddr {
    $self instvar dst_ipaddr_
    set dst_ipaddr_ [[Simulator instance] convert-ipaddr $dstipaddr]
    #puts "Agent $self set dstip $dst_ipaddr_"
    #$self dbdump
}

Agent instproc ipaddr { } {
    $self instvar dst_ipaddr_
    #puts "Agent $self checking ipaddr"
    if {![info exists dst_ipaddr_]} {
        # No destination assigned
        return 0
    } else {
        # exists, return actual value
        return $dst_ipaddr_
    }
}

Agent instproc set-dst-port dstport {
    $self instvar dst_ipport_
    #puts "Agent $self set dstport $dstport"
    set dst_ipport_ $dstport
}

# Procedure to return attached node object
Agent instproc mynode { } {
    $self node_
    return $node_
}

# Manage local ip address and port
Agent instproc set-ipaddr { myipaddr } {
    $self instvar my_ipaddr_
    set my_ipaddr_ $myipaddr
}

Agent instproc myipaddr { } {
    $self instvar my_ipaddr_
    return $my_ipaddr_
}

Agent instproc set-port { myport } {
    $self instvar my_port_
    set my_port_ $myport
}

Agent instproc myport { } {
    $self instvar my_port_
    return $my_port_
}

# Now some special things that just apply to Agent/RTIRouter

# Each RTIRouter agent maintains a list of ip addresses that it can
# reach locally.  HACK!  Need to maintain this automatically
#Class Agent/RTIRouter -superclass Agent
Agent/RTIRouter instproc add-local-ip { localip } {
    $self instvar ipaddr_list_
    lappend ipaddr_list_ $localip
    #puts "$self added local-ip [[Simulator instance] format-ipaddr $localip]"
}

# Returns TRUE if specified IP addr is local
Agent/RTIRouter instproc is-local-ip { whichip } {
    $self instvar ipaddr_list_
    #puts "Checking localip $whichip"
    if {![info exists ipaddr_list_]} {
        #puts "List empty"
        return 0
    } else {
        # debug follows
        #foreach i $ipaddr_list_ {
        #    puts [format "%08x" $i]
        #}
        #puts "iplist is $ipaddr_list_"
        set r [lsearch $ipaddr_list_ $whichip]
        #puts "r is $r"
        return [expr $r != -1 ]
    }
}

# Each RTI Router manages a list of "RLinks" (Remote links)
# each of which has an associated queue,
# which are conceptually simplex links, with associated delays
# and queues.  We also put the TTLChecker here, which is a bit different
# than normal ns's policy of checking TTL at the receiving end.
# This rlinks_ list is a list of lists, as follows:
# {ttlcheck, queue, rtilink, ipaddr, netmask, remote-target}
# The remote-target field is debug only...

Agent/RTIRouter instproc rlink { bw delay q ipaddr netmask } {
    $self instvar rlinks_ queue_ head_ link_ node_
    set link_ [new RTILink]
    $link_ set-ipaddr $ipaddr $netmask
    $link_ set bandwidth_ $bw
    $link_ set delay_ $delay

    set queue_ [new Queue/$q]
    #puts "rtirouter, queue $queue_ class is [$queue_ info class]"
    #puts "rtirouter, link $link_   class is   [$link_ info class]"
    $queue_ target $link_
    set ttl_ [new TTLChecker]
    $ttl_ target $queue_
    if { ![info exists rlinks_] } {
        # first rlink, make it the default
        $self add-default-remote-route $ttl_
    }
    lappend rlinks_ "$ttl_ $queue_ $link_ [$link_ ipaddr] [$link_ netmask] 0"
    set head_ $queue_
    # Finally note the ipaddress of this node in the ipaddr list
    $node_ set-ipaddr [$link_ ipaddr] [$link_ netmask] $link_
}

# Access the queue for a link
Agent/RTIRouter instproc get-queue { ipaddr } {
    $self instvar rlinks_
    set convip [[Simulator instance] convert-ipaddr $ipaddr]
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set thisip [lindex $l 3]
        if { $thisip == $convip} {
            #puts "Found rlink queue name [lindex $l 1]"
            return [lindex $l 1]
        }
    }
    puts "Can't find queue for ipaddr $ipaddr"
    return 0
}

# For each attached link, create an RTI multicast group for data tx
Agent/RTIRouter instproc create-groups { } {
    $self instvar rlinks_
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        $rtilink create-group $self [$rtilink broadcast-addr] $ipaddr
    }
}

# For each attached link, publish an RTI multicast group for data tx
Agent/RTIRouter instproc publish-groups { } {
    $self instvar rlinks_
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        $rtilink publish-group $self [$rtilink broadcast-addr] $ipaddr
    }
}

# For each attached link, join an RTI multicast group for data tx
Agent/RTIRouter instproc join-groups { } {
    $self instvar rlinks_
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        $rtilink join-group $self [$rtilink broadcast-addr] $ipaddr
    }
}

Agent/RTIRouter instproc rlinks { } {
    # Return the remote links list
    $self instvar rlinks_
    return $rlinks_
}

# For each attached link, find the minimum propogation delay
Agent/RTIRouter instproc min-lookahead { } {
    $self instvar rlinks_
    set mindelay 0
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set thisdelay [$rtilink set delay_]
        if { $i == 0} {
            set mindelay $thisdelay
        } else {
            if { $thisdelay < $mindelay } {
                set mindelay $thisdelay
            }
        }
    }
    return $mindelay
}

# Adds a routing table as computed by route.cc
# n is the tcl identifier of attached node, hopcounts is 
# a list indexed by nodeid of hopcounts to each node
Agent/RTIRouter instproc add-routing-table { n hopcounts } {
    $self instvar routetable_
    #puts "Setting routing table for $n to $hopcounts"
    set myid [$n id]
    for { set i 0 } { $i < [llength $hopcounts]} { incr i} {
        # process each node in the list
        set hc [lindex $hopcounts $i]
        set thisnode [[Simulator instance] get-node-by-id $i]
        # routes.cc miscalculates hopcount to self!
        if { $i == $myid } { set hc 0 } 
        #puts "Node $thisnode ipaddrcnt [$thisnode ipaddrcount]"
        if { [$thisnode ipaddrcount] != 0 } {
            # We only need info if the target node has ip addresses
            set ipaddrlist [$thisnode ipaddrlist]
            foreach ip $ipaddrlist {
                lappend routetable_ "[lindex $ip 0] $hc"
            }
        }
    }
    if { ![info exists routetable_] } {
        puts "$n No route table"
    } else {
        puts "$n routetable_ is "
        foreach r $routetable_ {
            puts "  [[Simulator instance] format-ipaddr [lindex $r 0]] [lindex $r 1]"

        }
    }
}

Agent/RTIRouter instproc get-route-table { } {
    $self instvar routetable_
    if { ![info exists routetable_] } {
        return ""
    } else {
        return $routetable_
    }
}

# Debug only...set the remote target.
# srcip identifies which remote link to connect from,
# targ/targip identifier which remote link to connect to
Agent/RTIRouter instproc rtarget { srcip targ targip } {
    $self instvar rlinks_
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        # We need to find which link to commumicate with this target
        if { [lindex $l 3] == $srcip} {
            # Found it
            #puts "Found correct ip $rlinks_"
            # change the target entry (5th element) to correct one
            set l [lreplace $l 5 5 $targ]
            set rlinks_ [lreplace $rlinks_ $i $i $l]
            # Also add a routing table entry pointing to ttl checker chain
            $self add-route $targip [lindex $l 0]
            # And point the rtarget of the link to the target
            set lk [lindex $l 2]
            $lk set-target $targ
            return
        }
    }
    puts "Could not find ip $srcip"
}
    
# Debug only...set the route of off-system targets
# In the real version, this will be done by the BGP protocol
# targip is the off-system target ip
# linkip is the ipaddr of the link to route on
# Agent/RTIRouter instproc add-route-via { targip linkip } {
#     $self instvar rlinks_
#     set targ_ipaddr_ [[Simulator instance] convert-ipaddr $targip]
#     set link_ipaddr_ [[Simulator instance] convert-ipaddr $linkip]
#     foreach this $rlinks_ {
#         if { [lindex $this 3] == $link_ipaddr_ } {
#             $self add-route $targ_ipaddr_ [lindex $this 0]
#             return
#         }
#     }
#     puts "Error! Can't find link ipaddr [[Simulator instance] format-ipaddr $link_ipaddr_]"
# }
