## LRU Dict

A fixed size dict like container which support ttl and evicts Least Recently 
Used (LRU) items once size limit is exceeded or the ttl is exceeded. 
LRU maximum capacity can be modified at run-time.

This is a project forked from https://github.com/amitdev/lru-dict and I added 
ttl feature for it which makes it more suitable to be a cache. If you want to
know more about the origin work, please go to the above url.


## Install

```shell
  pip install ttlru-dict
```
or
```shell
  easy_install ttlru_dict
```


## Usage

This can be used to build a LRU cache with TTL. Usage is almost like a dict.

```python
from ttlru import TTLRU
import time

# Create an TTLRU container that can hold 5 items, with default ttl = 1s
# *NOTE* ttl is represented in nanoseconds
l = TTLRU(5, ttl=1*1000000000)

# add an item with a default ttl, in this case, it's 1000000000ns = 1 second
l[0] = '0'

# add an item with specified ttl, in this case it's 2 seconds
l.set_with_ttl(1, '1', 2*1000000000)

print(l.items())
# Would print [(1, '1'), (0, '0')]

time.sleep(1)
print(l.items())
# Would print [(1, '1')], the first item expired.

time.sleep(1)
print(l.items())
# Would print [], the second item also expired after 2 seconds.

# ===================================================================

# orher operations without ttl
l = TTLRU(5)   

print(l.peek_first_item(), l.peek_last_item()) #return the MRU key and LRU key
# Would print None None

for i in range(5):
    l[i] = str(i)
print(l.items())    # Prints items in MRU order
# Would print [(4, '4'), (3, '3'), (2, '2'), (1, '1'), (0, '0')]

print(l.peek_first_item(), l.peek_last_item())  #return the MRU key and LRU key
# Would print (4, '4') (0, '0')

l[5] = '5'         # Inserting one more item should evict the old item
print(l.items())
# Would print [(5, '5'), (4, '4'), (3, '3'), (2, '2'), (1, '1')]

l[3]               # Accessing an item would make it MRU
print(l.items())
# Would print [(3, '3'), (5, '5'), (4, '4'), (2, '2'), (1, '1')]
# Now 3 is in front

print(l.keys())           # Can get keys alone in MRU order
# Would print [3, 5, 4, 2, 1]

del l[4]           # Delete an item
print(l.items())
# Would print [(3, '3'), (5, '5'), (2, '2'), (1, '1')]

print(l.get_size())
# Would print 5

l.set_size(3)
print(l.items())
# Would print [(3, '3'), (5, '5'), (2, '2')]
print(l.get_size())
# Would print 3
print(l.has_key(5))
# Would print True
print(2 in l)
# Would print True

print(l.get_stats())
# Would print (1, 0)


l.update({5: '0'})           # Update an item
print(l.items())
# Would print [(5, '0'), (3, '3'), (2, '2')]

l.clear()
print(l.items())
# Would print []

def evicted(key, value):
    print("removing: %s, %s" % (key, value))

l = TTLRU(1, callback=evicted)

l[1] = '1'
l[2] = '2'
# callback would print removing: 1, 1

l[2] = '3'
# doesn't call the evicted callback

print(l.items())
# would print [(2, '3')]

del l[2]
# doesn't call the evicted callback

print(l.items())
# would print []
```


## Notes and Technical Details

 *For more detailed information, please read the source code.*

### about ttl
* ttl = -1 means won't expire forever
* ttl is measured in nanoseconds. This is because Python's C-API `_PyTime_GetSystemClock` returns timestamp as nanoseconds, I don't want to do extra conversion.

### When will ttl be checked?
* ttl is checked everytime when you try to access it, if expired, ttlru will remove the current item, and try to return a not expired item if possible:
  * if you try to get a item like `result = l['a']`, if the item expired, `None` will returned.
  * if you try to get the first or the last item using `peek_first_item()` or `peek_last_item()`, if the first or the last item expired, it will try the next item and return the first item that is not expired. if all items is expired, then return `None`
  * `keys()`, `values()` and `items()` method will go through all the linked list and remove the expired items, then returns a list with not expired items.
* ttl is not checkd when you access the statistics data.
  * the `len(l)` will return count includes expired items. I think go through all the list to find not exist count is too expensive, maybe I can add another method to get the count of not expired items.


### What happened when insert an item?
* If the dict reached it's max size, then the last one will be removed. If there is an expired item but it is not the last one, then the expired item will still stay in the dict, only the last one will be removed. The reason is if I want to remove the expired one and keep the last one which is not expired, I have to use another data structure like a skip-table to keep the ttl order, which is not implemented in this version. For the other hand, the behavier above should be a *TTL-Dict*, not a *TTL-LRU-Dict*, Maybe I will write a *TTL-Dict* in the future.

### Different behavier against normal dict
* `keys()`, `values()` and `items()` returns a list, not a view object in Python3
