class Array
  def random
    return self[rand(self.size)]
  end
end

class String
  def uint8(offset = 0)
    self[offset].unpack("C").first
  end

  def uint16(offset = 0, code = "n")
    self[offset..offset+1].unpack(code).first
  end

  def le_uint16(offset = 0)
    uint16(offset, "v")
  end

  def uint32(offset = 0, code = "N")
    self[offset..offset+3].unpack(code).first
  end

  def le_uint32(offset = 0)
    uint32(offset, "V")
  end

  def cstring(offset = 0)
    self[offset..-1].split("\x00").first
  end
end
