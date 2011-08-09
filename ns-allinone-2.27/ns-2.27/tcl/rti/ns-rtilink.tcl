# Special case of Link for RTI interfacing
# George F. Riley.  Georgia Tech, Fall 1998

SimpleLink instproc ipaddr { } {
  $self instvar ipaddr_
  return $ipaddr_
}

SimpleLink instproc netmask { } {
  $self instvar netmask_
  return $netmask_
}

SimpleLink instproc set-ipaddr { ipaddr netmask } {
    $self instvar ipaddr_ netmask_
    set ipaddr_  [[Simulator instance] convert-ipaddr $ipaddr]
    set netmask_ [[Simulator instance] convert-ipaddr $netmask]
    #puts "SimpleLink::set-ipaddr to [[Simulator instance] format-ipaddr $ipaddr_]"
    # And set in the nodes ip address list
    set sn [$self src]
    #puts "SimpleLink::set-ipaddr sn $sn"
    $sn set-ipaddr $ipaddr_ $netmask_ $self
    # Added by gfr to maintain C++ list of links by src->dst
    #$self instvar link_ ; # Used only by OSC routines, not pdns
    #$link_ set-ipaddr $ipaddr_
}

SimpleLink instproc broadcast-addr { } {
  # Use the ipaddr and netmask to compute a broadcast addr
    $self instvar ipaddr_ netmask_
    set highbits [expr $ipaddr_ & $netmask_]
    set lowbits  [expr 0xffffffff & (~$netmask_)]
    return [expr $highbits | $lowbits]
}

#RTILinks are used only by class Agent/RTIRouter for off-system
#communications.
#Class RTILink -superclass DelayLink

RTILink instproc init { } {
  $self next instvar bandwidth_ delay_ 
  set bandwidth_ 1.5Mb
  set delay_ 10ms
}

RTILink instproc ipaddr { } {
  $self instvar ipaddr_
  return $ipaddr_
}

RTILink instproc netmask { } {
  $self instvar netmask_
  return $netmask_
}

RTILink instproc set-ipaddr { ipaddr netmask } {
    $self instvar ipaddr_ netmask_
    set ipaddr_  [[Simulator instance] convert-ipaddr $ipaddr]
    set netmask_ [[Simulator instance] convert-ipaddr $netmask]
}

RTILink instproc broadcast-addr { } {
  # Use the ipaddr and netmask to compute a broadcast addr
    $self instvar ipaddr_ netmask_
    set highbits [expr $ipaddr_ & $netmask_]
    set lowbits  [expr 0xffffffff & (~$netmask_)]
    return [expr $highbits | $lowbits]
}

# This is a debug-only proc.  For real off-system communication,
# RTIKit is used
RTILink instproc find-peer { } {
    foreach rp [Agent/RTIRouter info instances] {
        if { $rp != $self } {
            foreach rl [$rp rlinks] {
                # check each link
                if { [$rl broadcast-addr] == [$self broadcast-addr]} {
                    # found it
                    return $rl
                }
            }
        }
    }
    puts "No peer found for $self"
    return ""
}

#RTILink instproc set-bw { bw } {
#    $self bandwidth_ $bw
#    set bandwidth_ $bw
#}
#
#RTILink instproc set-delay { delay } {
#    $self delay_ $delay
#    set delay_ $delay
#}

# this is debug only.  set the ns target (single system debugging only)
#RTILink instproc set-target { targ } {
#    $self instvar rtarget_
#    set rtarget_ $targ
#}



