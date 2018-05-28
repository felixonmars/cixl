n = 100000

function f()
  for i=1,n do
    coroutine.yield(i)
  end
end

t1 = os.clock()
c = coroutine.create(f)

for i=1,n do
  _, j = coroutine.resume(c) 
  assert(i == j)
end

print((os.clock() - t1) * 1000)