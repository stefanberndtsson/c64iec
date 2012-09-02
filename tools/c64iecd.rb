#!/usr/bin/env ruby

$: << "."
require 'socket'
require 'pathname'
require 'stringio'
require 'common'
require 'device'
require 'tftp'

if __FILE__ == $0
  options = {}

  ARGV.each do |argv|
    if(argv[/^(\d+):(.*)/])
      id = $1.to_i
      path = Pathname.new($2)
      next if !path.directory? && path.extname.downcase != ".d64"
      options[:devices] ||= []
      options[:devices] <<= {
        :id => id,
        :path => path.to_s,
        :type => path.directory? ? :dir : :d64
      }
    else
      id = 8
      path = Pathname.new(argv)
      next if !path.directory? && path.extname.downcase != ".d64"
      options[:devices] = [{
        :id => id,
        :path => path.to_s,
        :type => path.directory? ? :dir : :d64
      }]
    end
  end

  iec = TFTP.run(options)
end
