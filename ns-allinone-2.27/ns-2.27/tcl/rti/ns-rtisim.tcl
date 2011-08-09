# Some extra simulator commands for distributed ns processing
# George F. Riley.  Georgia Tech, Fall 1998

# Used for timing tests...logs actual start of simulation (after all RTI init)
Simulator instproc sim-start { } {
    global simstart
    set simstart [clock seconds]
}

# In distributed ns, connections can be from a source node to a 
# non-local "ipaddress/port" pair.
Simulator instproc rconnect { src dstipaddr dstport } {
  #puts "Simulator rconnect src $src dstip $dstipaddr $dstport"
  $src set-dst-ipaddr $dstipaddr
  $src set-dst-port   $dstport
  # Following code obviates the "complete-remote-connections" stuff
  set thisnode [$src set node_]
  set defaultroute [$thisnode default-route]
  if { $defaultroute != "" } {
      $self connect $src $defaultroute
  }
  #puts "Sim::rconnect src $src defr $defaultroute"
}

# ALFRED ip-connect, alternative/replacement to rconnects
Simulator instproc ip-connect { src dstipaddr dstport } {
  $self instvar scheduler_
  set localnode [$scheduler_ get-local-ip $dstipaddr]
  set remotepath [$scheduler_ get-iproute $src $dstipaddr]
  #puts "localnode $localnode remotepath $remotepath"
  # local connect
  if {$localnode != 0 && $remotepath == 0} {
    set localagent [$localnode findport $dstport]
    if {$localagent != ""} {
      $self connect $src $localagent
      $src set-dst-ipaddr $dstipaddr
      $src set-dst-port   $dstport
      #puts [format "%.6f: Sim::ip-connect: lconnect $src -> $localagent ($dstipaddr:$dstport)" [$self now]]
    } else {
      puts [format "%.6f: Sim::ip-connect: FATAL: cannot find local agent for $dstipaddr:$dstport" [$self now]]
    }
  } else {
    # set connect now
    $scheduler_ iproute-connect $src $dstipaddr $dstport
    # remote connect
    $src set-dst-ipaddr $dstipaddr
    $src set-dst-port   $dstport
    #puts [format "%.6f: Sim::ip-connect: rconnect $src -> $dstipaddr:$dstport" [$self now]]
  }
}

# ALFRED add single command to aggregate route-via/pt/iproute together
Simulator instproc add-route { 
  router rip dst dmask { src 0.0.0.0 } { smask 0.0.0.0 } } {
  
  $self instvar scheduler_
  set ragent [$router set rtirouter_]
  $router add-route-via-src $dst $dmask $src $smask $rip
  $ragent add-route-passthrough $dst $dmask $src $smask $rip
  $scheduler_ add-iproute $dst $dmask $src $smask $ragent
}

#Simulator instproc is-local-ip { whichip } {
#    foreach this [Agent/RTIRouter info instances] {
#        if { $this is-local-ip $whichip } { return 1 } ; # found it
#    }
#    return 0; # Not found
#}

Simulator instproc add-local-ip { whichip node } {
    instvar scheduler_
    $scheduler_ add-local-ip $whichip $node ; # Scheduler does this
    # since there is a .cc for the scheduler that uses stl to track
    # IPAddress vs. ns Node.
}

Simulator instproc lookup-srcport { ipaddr daddr dport } {
    $self instvar scheduler_
    set n [$scheduler_ get-local-ip $ipaddr]
    if {$n == 0} { return 0 }
    foreach a [$n set agents_] {
        # Each agent assigned to n
        if { ([$a set dst_ipaddr_] == $daddr) &&
             ([$a set dst_ipport_] == $dport) } {
            # Found it
            return [$a set src_ipport_]
        }
    }
    return 0 ; # Nope, can't find it
}

# DEBUG ROUTINE ONLY...used for testing distns in single system
Simulator instproc debug-connect { src srcip dst dstip } {
    if {[$src info class] != "Agent/RTIRouter" ||
        [$dst info class] != "Agent/RTIRouter" } {
        puts "ERROR! debug-connect must connect RTIRouters"
        return;
    }
    set ns [Simulator instance]
    $src rtarget [$ns convert-ipaddr $srcip] $dst [$ns convert-ipaddr $dstip]
    $dst rtarget [$ns convert-ipaddr $dstip] $src [$ns convert-ipaddr $srcip]
}

# For debugging/timing testing only
# Determines of a forced-lookahead vlue is set
Simulator instproc forced-lookahead { } {
    $self instvar forced-lookahead-value
    return [info exists forced-lookahead-value]
}

# Calculates the lookahead value for this sim.
# The lookahead is the minimum of the propagation delay for all rlinks
Simulator instproc get-lookahead { } {
    set first 1
    set currentmin 0
    set nodelist [Node info instances]
    if { [ llength $nodelist ] == 0 } {
        # Try NixNodes
        set nodelist [Node/NixNode info instances]
    }
    foreach this $nodelist {
        set thismin [$this min-lookahead]
        #puts "Min from $this is $thismin"
        if { $first } {
            set currentmin $thismin
            set first 0
        } else {
            if {$thismin < $currentmin} {
                set currentmin $thismin
            }
        }
    }
    return $currentmin
}

# Specifies the "default default" router.
Simulator instproc add-route-default { rtidefaultroute } {
    $self instvar default_route_
    set default_route_ [$rtidefaultroute set rtirouter_]
}

Simulator instproc default-route { } {
    $self instvar default_route_
    if {![info exists default_route_]} {
        return ""
    } else {
        return $default_route_  
    }
}

Simulator instproc complete-remote-connections { } {
    #puts "Simulator::complete-remote-connections"
    set nodelist [Node info instances]
    if { [ llength $nodelist ] == 0 } {
        # Try NixNodes
        set nodelist [Node/NixNode info instances]
    }
    foreach thisnode $nodelist {
        #puts "Processing completions node $thisnode"
        foreach a [$thisnode set agents_] {
            #puts "Processing agent $a"
            set dstip [$a ipaddr]
            if { $dstip != 0 } {
                # found a remote connection
                #puts "Found remote connection to [[Simulator instance] format-ipaddr $dstip]"
                set defaultroute [$thisnode default-route]
                if { $defaultroute == "" } {
                    puts "No default route, skipping $a [[Simulator instance] format-ipaddr $dstip]"
                } else {
                    # create an ns connection to the default router
                    # DEBUG! Need to find best route
                    $self connect $a $defaultroute
                    #puts "Connecting to $defaultroute"
                }
            }
        }
    }
}

Simulator instproc convert-ipaddr ipaddr {
    set c [regsub -all \\. $ipaddr " " addrlist]
    if { $c == 0} { 
        return [expr $ipaddr]
    }
    if { [llength $addrlist] != 4} {
        puts "Ill-formatted ip address $ipaddr, addrlistlth = [llength $addrlist]"
        return 0
    }
    if { $c == 3 } {
        set r 0
        foreach a $addrlist {
            if { $a < 0 || $a > 255 } {
                puts "Ill-formatted ip address $ipaddr, a = $a"
                return 0
            }
            set r [expr ($r << 8) + $a]     
            # puts [format "inloop r is %02x %02x" $r $a]
        }
        return $r
    } else {
        puts "Ill-formatted ip address $ipaddr, c = $c"
        return 0
    }
}

Simulator instproc format-ipaddr ipaddr {
    set p1 [expr ($ipaddr >> 24) & 0xff]
    set p2 [expr ($ipaddr >> 16) & 0xff]
    set p3 [expr ($ipaddr >>  8) & 0xff]
    set p4 [expr ($ipaddr >>  0) & 0xff]
    return [format "%d\.%d\.%d\.%d" $p1 $p2 $p3 $p4]
}

# Locates the matching node for a specified ip address
Simulator instproc find-node-by-ip ipaddr {
    $self instvar Node_
    # nn is next available node index
    set nn [Node set nn_]
    for {set i 0} {$i < $nn} {incr i} {
        set n $Node_($i)
        # n is the node
        if {[$n ipmatch $ipaddr]} {
            # Found it
            return $n
        }
    }
    # Not found
    return 0
}

# Get the queue associated with an rlink
Simulator instproc rqueue { n1 addr } {
    # ALFRED - bug fix (comment next 4 lines)
    #$self instvar Node_
    #if { ![catch "$n1 info class Node"] } {
    #    set n1 [$n1 id]
    #}
    return [$n1 get-rqueue $addr]
}

Simulator instproc log-simstart { } {
        # GFR Modification to log actual start
        global simstart
        puts "Starting Actual Simulation"
        set simstart [clock seconds]
}

Simulator instproc unattach-agent { n a } {
    $n unattach $a
}
