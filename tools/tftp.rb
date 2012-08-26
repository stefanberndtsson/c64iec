require 'common'
require 'device'

class TFTP
  BLKSIZE=256
  OPCODES = [:NORQ, :RRQ, :WRQ, :DATA, :ACK, :ERR, :OACK]
  TYPEPOS = 0
  BLKPOS = 2
  FILEPOS = 2
  ERRMSGPOS = 4
  DATAPOS = 4

  def initialize(conn, devices, sender)
    @devices = devices
    @device_num = 8
    @conn = conn
    @cl_h = sender[3]
    @cl_p = sender[1]
    @mode = :netascii
    @blocknum = 0
    @blksize = BLKSIZE
  end

  def packet(data)
    STDERR.puts("DEBUG: Packet: #{data.size}")
    @status = :ready
    loop do
      @type = data.uint16(TYPEPOS)
      case OPCODES[@type]
      when :RRQ
        packet_rrq(data)
      when :WRQ
        packet_wrq(data)
      when :DATA
        packet_data(data)
      when :ACK
        packet_ack(data)
      when :ERR
        packet_err(data)
      when :OACK
        packet_oack(data)
      end

      if(@status == :wait_for_ack)
        @status = :ready
        data = wait_ack
        break if !data
      elsif(@status == :wait_for_data)
        @status = :ready
        data = wait_data
        break if !data
      elsif(@status == :error)
        STDERR.puts("DEBUG: ERROR CODE: #{@error_code}")
        STDERR.puts("DEBUG: ERROR Message: #{@error_msg}")
        break
      elsif(@status == :finished)
        break
      else
        break
      end
    end
  end

  def packet_rrq(data)
    @filename = data.cstring(FILEPOS)
    offset = @filename.size
    if(@filename[/^(\d+)\/(.*)/])
      @device_num = $1.to_i
      @filename = $2
    else
      @device_num = 8
    end

    tftp_options(data, FILEPOS+offset+1)

    @device = @devices[@device_num]

    if(!@device || !@device.open(@filename, :read))
      send_err(1, "File not found")
      return
    end

    if(@options.keys.empty?)
      send_data
    else
      send_oack
    end
  end

  def packet_wrq(data)
    @filename = data.cstring(FILEPOS)
    offset = @filename.size
    if(@filename[/^(\d+)\/(.*)/])
      @device_num = $1.to_i
      @filename = $2
    else
      @device_num = 8
    end

    tftp_options(data, FILEPOS+offset+1)

    @device = @devices[@device_num]
    if(!@device || @device.readonly || !@device.open(@filename, :write))
      send_err(1, "File not found")
      return
    end

    if(@options.keys.empty?)
      send_ack
    else
      send_oack(:wait_for_data)
    end
  end

  def packet_ack(data)
    @blocknum = data.uint16(BLKPOS)

    if @last_packet
      @blocknum = 0
      @device.close
      @status = :finished
      return
    end
    send_data(@send_empty)
  end

  def packet_data(data)
    @blocknum = data.uint16(BLKPOS)
    @device.write(data[DATAPOS..-1])
    send_ack
    if(data.size < @blksize)
      # End of data
      @device.close
      @status = :finished
      @blocknum = 0
    end
  end

  def packet_err(data)
    @error_code = data.uint16(BLKPOS)
    @error_msg = data.cstring(ERRMSGPOS)
    @status = :error
  end

  def tftp_options(data, offset)
    @options = {}
    while(offset < data.size)
      key = data.cstring(offset)
      STDERR.puts("DEBUG: key: #{key.inspect}")
      offset += key.size + 1
      if(["octet", "netascii"].include?(key))
        @mode = key
        next
      end
      value = data.cstring(offset)
      STDERR.puts("DEBUG: key: #{value.inspect}")
      @options[key] = value
      offset += value.size + 1
    end
    STDERR.puts("DEBUG: options: #{@options.inspect}")
  end

  def send_data(send_empty = false)
    @blocknum += 1
    if(send_empty)
      send(build_packet(:DATA, @blocknum))
      @last_packet = true
      @send_empty = false
    else
      data = @device.read(@blksize)
      send(build_packet(:DATA, @blocknum, data))
      if(!data || data.size < @blksize)
        @last_packet = true
        @send_empty = false
      elsif(@device.eof? && data.size == @blksize)
        @last_packet = false
        @send_empty = true
      else
        @last_packet = false
        @send_empty = false
      end
    end

    @status = :wait_for_ack
  end

  def send_oack(wait_status = :wait_for_ack)
    data = ""
    @options.keys.each do |option|
      data += option+"\x00"
      data += @options[option]+"\x00"
    end
    send(build_packet(:OACK, 0, data))
    @status = wait_status
  end

  def send_ack
    send(build_packet(:ACK, @blocknum))
    @status = :wait_for_data
  end

  def send_err(code, msg)
    send(build_packet(:ERR, code, msg+"\x00"))
    @status = :error
    @error_code = code
    @error_msg = msg
  end

  def wait_ack
    timeout_count = 0
    while not IO.select([@conn], nil, nil, 1)
      timeout_count += 1
      if(timeout_count >= 5)
        STDERR.puts("DEBUG: WAIT_ACK: #{timeout_count} seconds...aborting..")
        return nil
      end
    end
    data,sender = @conn.recvfrom(@blksize)
    STDERR.puts("DEBUG: ACK: data: #{data.inspect}")
    data
  end

  def wait_data
    timeout_count = 0
    while not IO.select([@conn], nil, nil, 1)
      timeout_count += 1
      if(timeout_count >= 5)
        STDERR.puts("DEBUG: WAIT_DATA: #{timeout_count} seconds...aborting..")
        return nil
      end
    end
    data,sender = @conn.recvfrom(@blksize)
    STDERR.puts("DEBUG: DATA: data: #{data.size}")
    data
  end

  def send(msg)
    STDERR.puts("DEBUG: Sending: #{msg.size} bytes to #{@cl_h}:#{@cl_p}")
    @conn.send(msg, 0, @cl_h, @cl_p)
  end

  def build_packet(type, blocknum_or_errorcode, data = nil)
    STDERR.puts("DEBUG: Build: #{type.inspect}: #{blocknum_or_errorcode.inspect}")
    pkt = ""
    pkt += [OPCODES.index(type)].pack("n")
    pkt += [blocknum_or_errorcode].pack("n")
    pkt += data if data
    pkt
  end

  def self.run(options = {})
    default_options = {
      :port => 53280,
      :devices => [
        {
          :id => 8,
          :path => "/var/tmp/c64iecd/8",
          :type => :dir
        },
        {
          :id => 9,
          :path => "/var/tmp/c64iecd/9",
          :type => :dir
        }
      ]
    }
    @options = default_options.merge(options)
    @devices = {}
    @options[:devices].each do |device|
      @devices[device[:id]] = Device.create(device[:id], device[:path], device[:type])
    end

    @udp = UDPSocket.new
    @udp.bind("0.0.0.0", @options[:port])
    loop do
      nil while not IO.select([@udp], nil, nil, 1)
      data,sender = @udp.recvfrom(TFTP::BLKSIZE)
      STDERR.puts("DEBUG: data: #{data.inspect}")
      tftp = TFTP.new(@udp, @devices, sender)
      tftp.packet(data)
      STDERR.puts("DEBUG: Closing #{@udp}")
      @udp.close
      STDERR.puts("DEBUG: Opening new UDPSocket")
      @udp = UDPSocket.new
      STDERR.puts("DEBUG: Binding port #{@options[:port]}")
      @udp.bind("0.0.0.0", @options[:port])
    end
  end
end
