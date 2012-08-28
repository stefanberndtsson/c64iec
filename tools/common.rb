class Array
  def random
    return self[rand(self.size)]
  end
end

class String
  def uint8(offset = 0)
    self[offset].unpack("C").first
  end

  def uint16(offset = 0)
    self[offset..offset+1].unpack("n").first
  end

  def uint32(offset = 0)
    self[offset..offset+3].unpack("N").first
  end

  def cstring(offset = 0)
    self[offset..-1].split("\x00").first
  end
end
