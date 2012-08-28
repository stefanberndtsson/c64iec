require 'common'

class C64File
  attr_reader :encoded, :c64, :path

  PC64_HEADER_SIZE=26
  PC64_FILENAME_SIZE=16
  PC64_ID_SIZE=7

  def initialize(encoded_c64_filename, path)
    @encoded = encoded_c64_filename
    @local = nil
    @c64 = decode
    @path = path
  end

  def decode
    @encoded.gsub(/%([a-fA-F0-9]{2})/) do |match|
      match = [$1].pack("H2")
    end.gsub(/^@\d:/,'')
  end

  def local(create = false)
    return @local if(@local)
    @local = find_local
    if(create)
      @local = create_local
    end
    @local
  end

  def find_local
    Pathname.new(@path).each_child do |child|
      next if !is_pc64?(child)
      return child if(pc64_c64_filename(child) == @c64)
    end
    nil
  end

  def create_local
    local_filename = local_encode
    File.open(file_path(local_filename), "wb") do |f|
      pc64_write_header(f)
    end
    Pathname.new(file_path(local_filename))
  end

  def local_encode
    stripped_filename = @c64.downcase.gsub(/[^0-9a-z_.-]/, "")
    pc64_counter = 0
    loop do
      if(!File.exist?(file_path(pc64_file(stripped_filename, pc64_counter))))
        return pc64_file(stripped_filename, pc64_counter)
      end
      pc64_counter += 1
      return nil if(pc64_counter > 99)
    end
  end

  def is_pc64?(file)
    return false if(!file.file? || !file.extname[/\.p\d\d$/i])
    pc64_header = nil
    File.open(file.to_s) do |f|
      pc64_header = f.read(PC64_HEADER_SIZE)
    end
    return false if(!pc64_header || pc64_header[0,PC64_ID_SIZE] != "C64File")
    true
  end

  def pc64_c64_filename(file)
    filename = C64File.pc64_c64_filename(file)
    STDERR.puts("DEBUG: Matching #{filename.inspect} with #{@c64.inspect}")
    filename
  end

  def self.pc64_c64_filename(file)
    pc64_header = nil
    File.open(file.to_s) do |f|
      pc64_header = f.read(PC64_HEADER_SIZE)
    end
    pc64_header[8,PC64_FILENAME_SIZE].gsub(/[ \xa0\x00]*$/,"")
  end

  def pc64_write_header(file)
    c64_name = @c64+"\x00"*(PC64_FILENAME_SIZE-@c64.size)
    file.write("C64File\x00#{c64_name}\x00\x00")
  end

  def pc64_file(filename, counter)
    filename+".p"+sprintf("%02d", counter)
  end

  def file_path(filename)
    "#{@path}/#{filename}"
  end
end

class Device
  attr_accessor :id, :path, :type, :readonly

  def initialize(id, path, type, readonly)
    @id = id
    @path = path
    @type = type
    @readonly = readonly
    @c64file = nil
  end

  def self.create(id, path, type)
    return DeviceDir.new(id, path, type, false) if type == :dir
    return DeviceD64.new(id, path, type, true) if type == :d64
  end
end

class DeviceDir < Device
  BLKSIZE=256

  def exist?
    return true if(@c64file.c64 == "$")
    STDERR.puts("DEBUG: exist?(), local: #{@c64file.local.inspect}")
    !!@c64file.local
  end

  def open(filename, mode = :read)
    @c64file = C64File.new(filename, @path)
    return open_read if mode == :read
    return open_write if mode == :write
  end

  def open_read
    return false if(!exist?)
    if(@c64file.c64 == "$")
      @file = StringIO.new(directory(@path))
      return true
    end

    STDERR.puts("DEBUG: open_read(), local: #{@c64file.local}")
    @file = File.open(@c64file.local, "rb")
    @file.read(C64File::PC64_HEADER_SIZE)
    return true
  end

  def open_write
    local = @c64file.local
    if(local)
      local.unlink
    end
    @file = File.open(@c64file.create_local, "ab")
    STDERR.puts("DEBUG: open_write(#{@c64file.c64}) == #{@file}")
    return true
  end

  def close
    @file.close
    @c64file = nil
  end

  def read(bytes)
    @file.read(bytes)
  end

  def write(data)
    STDERR.puts("DEBUG: Writing #{data.size} bytes...")
    @file.write(data)
  end

  def eof?
    @file.eof?
  end

  def directory(path)
    @addr = 0x0801
    dir_data = dir_header
    return dir_data+dir_footer if !Pathname.new(path).exist?
    Pathname.new(path).each_child do |child|
      next if !child.file? || child.extname.downcase != ".p00"
      dir_data += dir_entry(child)
    end
    dir_data += dir_footer
    return dir_data
  end

  def dir_header
    # Fake PC64 header
    header = "C64File\x00DIRECTORY\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    header = ""
    STDERR.puts("DEBUG: #{header.size}")
    header += [@addr].pack("v")
    len = "DIRECTORY".size
    @addr += len+5
    header += [@addr].pack("v")
    header += "\x00\x00\x12\"DIRECTORY\"\x00"
    header
  end

  def dir_entry(file)
    # "#{file.basename.to_s}: #{file.size}\n"
    size = (file.size-C64File::PC64_HEADER_SIZE)/BLKSIZE
    filename = C64File.pc64_c64_filename(file)
    b = 1
    b += 1 if(size < 100)
    b += 1 if(size < 10)
    @addr += 26+b
    entry = [@addr].pack("v")
    entry += [size].pack("v")
    entry += " "*b
    filename,padding = rewrite_filename(filename)
    entry += "\"#{filename}\""
    entry += padding
    if(file.extname.downcase == ".p00")
      entry += " PRG "
    else
      entry += " UKN "
    end
    entry += "\x00"
    entry
  end

  def rewrite_filename(filename)
    reduced = filename.gsub(/[\x00\xa0 ]*$/,"")
    STDERR.puts("DEBUG: reduced: #{reduced.inspect}")
    tmp = [reduced," "*(C64File::PC64_FILENAME_SIZE-reduced.size)]
    STDERR.puts("DEBUG: returned: #{tmp.inspect}")
    tmp
  end

  def dir_footer
    footer = [@addr].pack("v")
    footer += "\xff\xffBLOCKS FREE.\x00\x00\x00"
    footer
  end
end

class DeviceD64 < Device
  BLKSIZE=256
  SECTORS=[21]*17 + [19]*7 + [18]*6 + [17]*10
  FILE_TYPE_PRG=2

  def open(filename, mode = :read)
    read_disk
  end

  def read(bytes)
  end

  def close
  end

  def directory
    entries = collect_directory_entries(18, 1)
  end

  def collect_directory_entries(track, sector)
    entries = []
    blk = read_block(track, sector)
    next_track = blk.uint8(0)
    next_sector = blk.uint8(1)
    offset = 0
    loop do
      entries << blk[offset,32] if blk.uint8(offset+2)&0xf == FILE_TYPE_PRG
      offset += 32
      if(offset > BLKSIZE)
        offset = 0
        break if next_track == 0
        read_block(next_track, next_sector)
      end
    end
    entries
  end

  def read_block(track, sector)
    read_disk
    @d64[block(track, sector)*BLKSIZE,BLKSIZE]
  end

  def block(track, sector)
    block = 0
    return sector if track == 1
    (track-1).downto(1) do |tr|
      block += SECTORS[tr-1]
    end
    block += sector
  end

  def read_disk
    if(!@d64)
      File.open(@path, "rb") do |f|
        @d64 = f.read
      end
    end
    STDERR.puts("DEBUG: Invalid size: #{@d64.size}") if @d64.size != 174848
  end
end
