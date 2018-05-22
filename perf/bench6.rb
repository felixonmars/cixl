n = 10000
fs = []

n.times do
  fs << Fiber.new {}
end

t1 = Time.now
fs.each {|f| f.resume}
t2 = Time.now
delta = (t2 - t1) * 1000
puts "#{delta.to_i}"
