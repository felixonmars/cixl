n = 100000

c = Fiber.new {(0..n).each {|i| Fiber.yield(i)}}

t1 = Time.now
(0..n).each {|i| raise 'fail' unless c.resume == i}
t2 = Time.now
delta = (t2 - t1) * 1000
puts "#{delta.to_i}"
