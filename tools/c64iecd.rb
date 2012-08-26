#!/usr/bin/env ruby

$: << "."
require 'socket'
require 'pathname'
require 'stringio'
require 'common'
require 'device'
require 'tftp'

if __FILE__ == $0
  iec = TFTP.run()
end
