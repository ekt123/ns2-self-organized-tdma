# Special case of Node for RTI interfacing
# George F. Riley.  Georgia Tech, Summer 1998

# Port management routines.  Emulate the unix "bind" option
# and maintain a list of agent/port tuples

# Locate a port in the portlist
# The portlist is a list of tuples, 0th is the portnum 1st is agent
Node instproc findport { port } {
    $self instvar portlist_
    if {![info exists portlist_]} {
        # No existing ports
        return ""
    }
    foreach p $portlist_ {
        if { $port == [lindex $p 0] } {
            # Found the correct port, return the agent id
            return [lindex $p 1]
        }
    }
    return ""
}

Node instproc find-srcport { ipaddr port } {
    $self instvar agents_
    #set decip [expr $ipaddr]
    #set aip [[Simulator instance] format-ipaddr $ipaddr]

    foreach a $agents_ {
        set daddr 0
        set dport -2
        catch { set daddr [$a set dst_ipaddr_] }
        catch { set dport [$a set dst_ipport_  ] }
        set dip [[Simulator instance] format-ipaddr $daddr]
        #puts "Comparing $dip to $aip and $dport to $port"
        #if { ( [string compare $aip $dip] == 0 ) && ($dport == $port) } {
        if { ( $ipaddr == $daddr ) && ($dport == $port) } {
            # Found it
            return [$a set my_port_]
        }
    }
    return -1 ; # Nope, not found
}

# debug routine to dump portlist
Node instproc dumports { } {
    $self instvar portlist_
    foreach p $portlist_ {
        puts "Agent [lindex $p 1] bound to [lindex $p 0]"
    }
}

# Bind an agent to a particular port
# If port = 0, choose an available one
Node instproc bind { agent port } {
    #puts "Node $self binding agent $agent to port $port"
    $self instvar nextport_ portlist_
    if {$port == 0} {
        # Bind to next available port
        if {![info exists nextport_]} {
            # first time, set to initial value
            set nextport_ 10000
            #puts "Initializing nextport"
        }
        set thisport $nextport_
        incr nextport_
    } else {
        set thisport $port
    }
    # Now add port/agent pair to current list
    set existing [$self findport $thisport]
    if { $existing != "" } {
        puts "Can't bind to port $thisport, already bound to $existing"
        return 0
    }
    lappend portlist_ " $thisport $agent "
    # inform the agent of bound port and ipaddr
    $agent set-port $thisport
    $agent set-ipaddr [$self ipaddr]
    #puts "node $self agent $agent, set port $thisport ipaddr [$self ipaddr]"
    return $thisport
}

# Find the agent bound to a particular port on this node
Node instproc get-agent-by-port { port } {
    #puts "Searching for agent at port $port"
    $self instvar portlist_
    if {![info exists portlist_]} {
        return 0
    }
    #puts "Node $self agentportlist $portlist_"
    # whichind is the index into the matching port/agent list
    set whichind [lsearch $portlist_ "$port *"]
    #puts "Whichind $whichind"
    if {$whichind == -1} {
        # not found
        return 0
    }
    #puts "Found agent/port ind $whichind"
    # agentport is the "port agent" pair
    set agentport [lindex $portlist_ $whichind]
    return [lindex $agentport 1]
}

# For distributed ns, nodes have the concept of "ip address".
# Actually IP addresses are assigned to links, so the node just
# gets a list of IP addresses, matching the addresses of the connected
# links.  Also stores the netmask (useless I think!) and the node name of
# connected link.

Node instproc set-ipaddr { ipaddr netmask whichlink } {
    $self instvar ipaddrlist_
    #puts "Node $self adding ipaddr [[Simulator instance] format-ipaddr $ipaddr]"
    set ipa [[Simulator instance] convert-ipaddr $ipaddr]
    lappend ipaddrlist_ "$ipa $netmask $whichlink"
    catch {
        #puts "Node $self setting ipa [[Simulator instance] format-ipaddr $ipa]"
        [[Simulator instance] set scheduler_] add-local-ip $ipa $self
    }
		# Assign local ip to all agents if they don't already have one
		$self instvar agents_
		foreach a $agents_ {
		    set myip [$a set my_ipaddr_]
		    if { $myip == 0} {
		         puts "Node $self found agent $a without ip, assigning $ipaddr"
			       $a set my_ipaddr_ $ipaddr
			  }
		}
}

# Returns the first value in the ipaddr list
Node instproc ipaddr { } {
    $self instvar ipaddrlist_
    if {![info exists ipaddrlist_]} {
        $self instvar address_
        return $address_ ; # Return the address if no ipaddrlist
    }
    set first [lindex $ipaddrlist_ 0]
    return [lindex $first 0]
}

Node instproc ipaddrlist { } {
    $self instvar ipaddrlist_
    return $ipaddrlist_
}

# Returns the number of ipaddresses in a node's list
Node instproc ipaddrcount { } {
    $self instvar ipaddrlist_
    if {![info exists ipaddrlist_]} {
        return 0
    }
    return [llength $ipaddrlist_]
}

# Determines if a given node matches the specified IP address
Node instproc ipmatch { whichip } {
    $self instvar ipaddrlist_
    #puts "Checking ipmatch for node $self against $whichip"
    if {![info exists ipaddrlist_]} {
        # No ip addresses for this node, can't possibly match
        return 0;
    }
    #puts "IPAddrList $ipaddrlist_"
    set formattedip [[Simulator instance] convert-ipaddr $whichip]
    set whichind [lsearch $ipaddrlist_ "$formattedip * *"]
    if {$whichind == -1} {
        # Not in list, return false
        return 0;
    } else {
        return 1;  
    }
}

# Returns the iplist list for the specified ip address
Node instproc ipinfo { whichip } {
    $self instvar ipaddrlist_
    set formattedip [[Simulator instance] convert-ipaddr $whichip]
    set whichind [lsearch $ipaddrlist_ "$formattedip * *"]
    if {$whichind == -1} {
        # Not in list, return junk
        return { 0 0 ""}
    } else {
        return [lindex $ipaddrlist_ $whichind]
    }
}


# Distributed ns has a default route for each node.  If no
# default route is defined for a node, then the global default
# route is used
Node instproc add-route-default { rtidefaultroute } {
    $self instvar default_route_ rtirouter_
    set default_route_ [$rtidefaultroute set rtirouter_]
    #puts "Added default route [$rtidefaultroute set rtirouter_] for node $self"
}

Node instproc default-route { } {
    $self instvar default_route_
    if {![info exists default_route_]} {
        # No node specific default route, return system default
        #puts "No default route for node $self, checking default-default"
        return [[Simulator instance] default-route]
    } else {
        return $default_route_  
    }
}

# Allows a node to specify it would like the routing table
# computed by routes.cc.  "$node set NeedRoutes 1" says you 
# want them
Node instproc NeedRoutes? { } {
    $self instvar NeedRoutes
    return [info exists NeedRoutes]
}

Node instproc SetRoutes { routelist } {
    #puts "$self ([$self id]) got routelist $routelist"
    # First find if we have an Agent/RTIRouter on this node
    # If not, just ignore
    $self instvar agents_
    if {[llength $agents_] == 0} {
        #puts "No agents on node $self, exiting"
        return
    }
    foreach agent $agents_ {
        if { [$agent info class] == "Agent/RTIRouter" } {
            #puts "Found RTIRouter ($agent) on $self"
            $agent add-routing-table $self $routelist
            return
        }
    }
    #puts "No RTIRouter found on $self"
}

# The following to manage rlinks are moved from Agent/RTIRouter
# It makes more sense for the rlinks to be a function of nodes, not agents
# Each edge node  manages a list of "RLinks" (Remote links)
# each of which has an associated queue,
# which are conceptually simplex links, with associated delays
# and queues.  We also put the TTLChecker here, which is a bit different
# than normal ns's policy of checking TTL at the receiving end.
# This rlinks_ list is a list of lists, as follows:
# {ttlcheck, queue, rtilink, ipaddr, netmask, remote-target}
# The remote-target field is debug only...
# This instproc returns the actual RTILink object
Node instproc rlink { bw delay q ipaddr netmask } {
    #puts "Node::rlink bw $bw delay $delay"
    $self instvar rlinks_ queue_ link_ node_ rtirouter_
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
    if {![info exists rtirouter_] } {
        # Need to create the RTIRouter for this node
        set rtirouter_ [new Agent/RTIRouter]
        [Simulator instance] attach-agent $self $rtirouter_
        #puts "Node $self created rtirouter_ $rtirouter_"
    }
    if { ![info exists rlinks_] } {
        # first rlink, make it the default
        $rtirouter_ add-default-remote-route $ttl_
    }
    lappend rlinks_ "$ttl_ $queue_ $link_ [$link_ ipaddr] [$link_ netmask] 0"
    $link_ set head_ $queue_
    # Finally note the ipaddress of this node in the ipaddr list
    $self set-ipaddr [$link_ ipaddr] [$link_ netmask] $link_
    return $link_
}

# Access the queue for a remote link
Node instproc get-rqueue { ipaddr } {
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
Node instproc create-groups { } {
    $self instvar rlinks_ rtirouter_
    if {! [info exists rlinks_] } {
        return ; # No rlinks, nothing to do
    }
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        set bc [$rtilink broadcast-addr]
        $rtilink create-group $rtirouter_ $bc $ipaddr
    }
}

# For each attached link, publish an RTI multicast group for data tx
Node instproc publish-groups { } {
    $self instvar rlinks_ rtirouter_
    if {! [info exists rlinks_] } {
        return ; # No rlinks, nothing to do
    }
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        set bc [$rtilink broadcast-addr]
        $rtilink publish-group $rtirouter_ $bc $ipaddr
    }
}

# For each attached link, join an RTI multicast group for data tx
Node instproc join-groups { } {
    $self instvar rlinks_ rtirouter_
    if {! [info exists rlinks_] } {
        return ; # No rlinks, nothing to do
    }
    for { set i 0 } { $i < [llength $rlinks_] } {incr i} {
        set l [lindex $rlinks_ $i]
        set rtilink [lindex $l 2]
        set ipaddr [lindex $l 3]
        set bc [[Simulator instance] format-ipaddr [$rtilink broadcast-addr]]
        $rtilink join-group $rtirouter_ $bc $ipaddr
    }
}

Node instproc rlinks { } {
    # Return the remote links list
    $self instvar rlinks_
    return $rlinks_
}

# For each attached link, find the minimum propogation delay
Node instproc min-lookahead { } {
    $self instvar rlinks_
    if {! [info exists rlinks_] } {
        return 999999; # No rlinks, use large lookahead
    }
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

# Debug only...set the route of off-system targets
# In the final version, this will be done by the BGP protocol
# targip is the off-system target ip
# linkip is the ipaddr of the link to route on
# If a target mask is specified, the routes are aggregated with the
#   mask specified.  This allows all ip addresses beginning with a prefix
#   to be routed via the same link.
Node instproc add-route-via { targip linkip { targmask 0xffffff00 } } {
    #$self instvar rlinks_ rtirouter_
    #set targ_ipaddr_ [[Simulator instance] convert-ipaddr $targip]
    #set link_ipaddr_ [[Simulator instance] convert-ipaddr $linkip]
    #foreach this $rlinks_ {
    #    if { [lindex $this 3] == $link_ipaddr_ } {
    #        $rtirouter_ add-route $targ_ipaddr_ [lindex $this 0] \
    #            [[Simulator instance] convert-ipaddr $targmask]
    #        return
    #    }
    #}
    #puts "Error! Can't find link ipaddr [[Simulator instance] format-ipaddr $link_ipaddr_]"

    # ALFRED use add-route-via-src below instead with defaults:
    #    srcip 0.0.0.0 and srcmask 0.0.0.0
    $self add-route-via-src $targip $targmask 0.0.0.0 0.0.0.0 $linkip
}

# ALFRED add add-route-via-src support
Node instproc add-route-via-src { dst dm src sm targ } {
  $self instvar rlinks_ rtirouter_
  set dstip [[Simulator instance] convert-ipaddr $dst]
  set dmask [[Simulator instance] convert-ipaddr $dm]
  set srcip [[Simulator instance] convert-ipaddr $src]
  set smask [[Simulator instance] convert-ipaddr $sm]
  set rip [[Simulator instance] convert-ipaddr $targ]
  foreach this $rlinks_ {
    if { [lindex $this 3] == $rip } {
      $rtirouter_ add-route $srcip $smask $dstip $dmask [lindex $this 0]
      return
    }
  }
  puts "Node($self): Warning! Can't find $targ link!"
}

Node instproc unattach { a } {
    # Delete's an entry from the agents list
    $self instvar agents_
    set i [lsearch $agents_ $a]
    if { $i >= 0} {
        if { $i == 0 } {
            # delete first
            set agents_ [lrange $agents_ 1 end]
        } else {
            set im1 [expr $i - 1]
            set ip1 [expr $i + 1]
            set agents_ [concat [lrange $agents_ 0 $im1] \
                                [lrange $agents_ $ip1 end] ]
        }
    }
}

Node instproc exchange-routes { } {
    # Run the run-time route exchange (not coded yet)
    $self instvar rtirouter_
    if { [info exists rtirouter_] } {
        $rtirouter_ exchange-routes
    }
}

# ALFRED add droptarget support
Node instproc get-droptarget { } {
  $self instvar dt_
  if {![info exists dt_]} {
    set dt_ [new DropTargetAgent]
    [Simulator instance] attach-agent $self $dt_
  }
  return $dt_
}

# The following are leftover from the original hack implementation
# and no longer of value
# Node instproc set-input-port inport {
#   $self instvar inport_
#   set inport_ $inport
# }

# Node instproc set-output-port outport {
#   $self instvar outport_
#   set outport_ $outport
# }

# Node instproc create-groups { } {
#   $self instvar outport_
#   if { [info exists outport_] } {
#       #puts "Node $self outport_ $outport_"
#       set rt [Agent/RTI info instances]
#       #puts "Number RTIAgents is [llength $rt]"
#       set na [llength $rt]
#       # HACK!  Need to code for multiple RTI Agents
#       if {$na == 1} {
#           #puts "rt $rt"
#           #set rti [lindex $rt 1]
#           #puts "creating group agent $rti port $outport_"
#           $rt create-group $outport_ $self
#       }
#   } else {
#       #puts "Node $self no outport"
#   }
# }

# Node instproc join-groups { } {
#   $self instvar inport_
#   if { [info exists inport_] } {
#       # puts "Node $self inport_ $inport_"
#       set rt [Agent/RTI info instances]
#       # puts "Number RTIAgents is [llength $rt]"
#       set na [llength $rt]
#       # HACK!  Need to code for multiple RTI Agents
#       if {$na == 1} {
#           #set rti [lindex $rt 1]
#           $rt join-group $inport_ $self
#       }
#   } else {
#       #puts "Node $self no inport"
#   }
# }


