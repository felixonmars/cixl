n = 1000
fs = []

n.times {fs << Fiber.new {Fiber.yield}}

t1 = Time.now
2.times {fs.each {|f| f.resume}}
t2 = Time.now
delta = (t2 - t1) * 1000
puts "#{delta.to_i}"
