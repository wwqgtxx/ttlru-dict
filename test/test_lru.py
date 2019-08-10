import gc
import random
import sys
import unittest
import time
from ttlru import TTLRU

SIZES = [1, 2, 10, 1000]

# Only available on debug python builds.
gettotalrefcount = getattr(sys, 'gettotalrefcount', lambda: 0)

class TestTTLRU(unittest.TestCase):

    def setUp(self):
        gc.collect()
        self._before_count = gettotalrefcount()

    def tearDown(self):
        after_count = gettotalrefcount()
        self.assertEqual(self._before_count, after_count)

    def _check_kvi(self, valid_keys, l):
        valid_keys = list(valid_keys)
        valid_vals = list(map(str, valid_keys))
        self.assertEqual(valid_keys, l.keys())
        self.assertEqual(valid_vals, l.values())
        self.assertEqual(list(zip(valid_keys, valid_vals)), l.items())

    def test_invalid_size(self):
        self.assertRaises(ValueError, TTLRU, -1)
        self.assertRaises(ValueError, TTLRU, 0)

    def test_empty(self):
        l = TTLRU(1)
        self.assertEqual([], l.keys())
        self.assertEqual([], l.values())

    def test_add_within_size(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)
            self._check_kvi(range(size-1,-1,-1), l)

    def test_delete_multiple_within_size(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)
            for i in range(0,size,2):
                del l[i]
            self._check_kvi(range(size-1,0,-2), l)
            for i in range(0,size,2):
                with self.assertRaises(KeyError):
                    l[i]

    def test_delete_multiple(self):
        for size in SIZES:
            l = TTLRU(size)
            n = size*2
            for i in range(n):
                l[i] = str(i)
            for i in range(size,n,2):
                del l[i]
            self._check_kvi(range(n-1,size,-2), l)
            for i in range(0,size):
                with self.assertRaises(KeyError):
                    l[i]
            for i in range(size,n,2):
                with self.assertRaises(KeyError):
                    l[i]

    def test_add_multiple(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)
            l[size] = str(size)
            self._check_kvi(range(size,0,-1), l)

    def test_access_within_size(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)
            for i in range(size):
                self.assertEqual(l[i], str(i))
                self.assertEqual(l.get(i,None), str(i))

    def test_contains(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)
            for i in range(size):
                self.assertTrue(i in l)

    def test_access(self):
        for size in SIZES:
            l = TTLRU(size)
            n = size * 2
            for i in range(n):
                l[i] = str(i)
            self._check_kvi(range(n-1,size-1,-1), l)
            for i in range(size, n):
                self.assertEqual(l[i], str(i))
                self.assertEqual(l.get(i,None), str(i))


    def test_update(self):
        l = TTLRU(2)
        l['a'] = 1
        self.assertEqual(l['a'], 1)
        l.update(a=2)
        self.assertEqual(l['a'], 2)
        l['b'] = 2
        self.assertEqual(l['b'], 2)
        l.update(b=3)
        self.assertEqual(('b', 3), l.peek_first_item())
        self.assertEqual(l['a'], 2)
        self.assertEqual(l['b'], 3)
        l.update({'a':1, 'b':2})
        self.assertEqual(('b', 2), l.peek_first_item())
        self.assertEqual(l['a'], 1)
        self.assertEqual(l['b'], 2)
        l.update()
        self.assertEqual(('b', 2), l.peek_first_item())
        l.update(a=2)
        self.assertEqual(('a', 2), l.peek_first_item())


    def test_peek_first_item(self):
        l = TTLRU(2)
        self.assertEqual(None, l.peek_first_item())
        l[1] = '1'
        l[2] = '2'
        self.assertEqual((2, '2'), l.peek_first_item())

    def test_peek_last_item(self):
        l = TTLRU(2)
        self.assertEqual(None, l.peek_last_item())
        l[1] = '1'
        l[2] = '2'
        self.assertEqual((1, '1'), l.peek_last_item())

    def test_overwrite(self):
        l = TTLRU(1)
        l[1] = '2'
        l[1] = '1'
        self.assertEqual('1', l[1])
        self._check_kvi([1], l)

    def test_has_key(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(2*size):
                l[i] = str(i)
                self.assertTrue(l.has_key(i))
            for i in range(size, 2*size):
                self.assertTrue(l.has_key(i))
            for i in range(size):
                self.assertFalse(l.has_key(i))

    def test_capacity_get(self):
        for size in SIZES:
            l = TTLRU(size)
            self.assertTrue(size == l.get_size())

    def test_capacity_set(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size+5):
                l[i] = str(i)
            l.set_size(size+10)
            self.assertTrue(size+10 == l.get_size())
            self.assertTrue(len(l) == size)
            for i in range(size+20):
                l[i] = str(i)
            self.assertTrue(len(l) == size+10)
            l.set_size(size+10-1)
            self.assertTrue(len(l) == size+10-1)

    def test_unhashable(self):
        l = TTLRU(1)
        self.assertRaises(TypeError, lambda: l[{'a': 'b'}])
        with self.assertRaises(TypeError):
            l[['1']] = '2'
        with self.assertRaises(TypeError):
            del l[{'1': '1'}]

    def test_clear(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size+5):
                l[i] = str(i)
            l.clear()
            for i in range(size):
                l[i] = str(i)
            for i in range(size):
                _ = l[random.randint(0, size-1)]
            l.clear()
            self.assertTrue(len(l) == 0)

    def test_get_and_del(self):
        l = TTLRU(2)
        l[1] = '1'
        self.assertEqual('1', l.get(1))
        self.assertEqual('1', l.get(2, '1'))
        self.assertIsNone(l.get(2))
        self.assertEqual('1', l[1])
        self.assertRaises(KeyError, lambda: l['2'])
        with self.assertRaises(KeyError):
            del l['2']

    def test_stats(self):
        for size in SIZES:
            l = TTLRU(size)
            for i in range(size):
                l[i] = str(i)

            self.assertTrue(l.get_stats() == (0, 0))

            val = l[0]
            self.assertTrue(l.get_stats() == (1, 0))

            val = l.get(0, None)
            self.assertTrue(l.get_stats() == (2, 0))

            val = l.get(-1, None)
            self.assertTrue(l.get_stats() == (2, 1))

            try:
                val = l[-1]
            except:
                pass

            self.assertTrue(l.get_stats() == (2, 2))

            l.clear()
            self.assertTrue(len(l) == 0)
            self.assertTrue(l.get_stats() == (0, 0))

    def test_lru(self):
        l = TTLRU(1)
        l['a'] = 1
        l['a']
        self.assertEqual(l.keys(), ['a'])
        l['b'] = 2
        self.assertEqual(l.keys(), ['b'])

        l = TTLRU(2)
        l['a'] = 1
        l['b'] = 2
        self.assertEqual(len(l), 2)
        l['a']                  # Testing the first one
        l['c'] = 3
        self.assertEqual(sorted(l.keys()), ['a', 'c'])
        l['c']
        self.assertEqual(sorted(l.keys()), ['a', 'c'])

        l = TTLRU(3)
        l['a'] = 1
        l['b'] = 2
        l['c'] = 3
        self.assertEqual(len(l), 3)
        l['b']                  # Testing the middle one
        l['d'] = 4
        self.assertEqual(sorted(l.keys()), ['b', 'c', 'd'])
        l['d']                  # Testing the last one
        self.assertEqual(sorted(l.keys()), ['b', 'c', 'd'])
        l['e'] = 5
        self.assertEqual(sorted(l.keys()), ['b', 'd', 'e'])

    def test_callback(self):

        counter = [0]

        first_key = 'a'
        first_value = 1

        def callback(key, value):
            self.assertEqual(key, first_key)
            self.assertEqual(value, first_value)
            counter[0] += 1

        l = TTLRU(1, callback=callback)
        l[first_key] = first_value
        l['b'] = 1              # test calling the callback

        self.assertEqual(counter[0], 1)
        self.assertEqual(l.keys(), ['b'])

        l['b'] = 2              # doesn't call callback
        self.assertEqual(counter[0], 1)
        self.assertEqual(l.keys(), ['b'])
        self.assertEqual(l.values(), [2])


        l = TTLRU(1, callback=callback)
        l[first_key] = first_value

        l.set_callback(None)
        l['c'] = 1              # doesn't call callback
        self.assertEqual(counter[0], 1)
        self.assertEqual(l.keys(), ['c'])

        l.set_callback(callback)
        del l['c']              # doesn't call callback
        self.assertEqual(counter[0], 1)
        self.assertEqual(l.keys(), [])

        l = TTLRU(2, callback=callback)
        l['a'] = 1              # test calling the callback
        l['b'] = 2              # test calling the callback

        self.assertEqual(counter[0], 1)
        self.assertEqual(l.keys(), ['b', 'a'])
        l.set_size(1)
        self.assertEqual(counter[0], 2) # callback invoked
        self.assertEqual(l.keys(), ['b'])

    def test_default_ttl(self):
        l = TTLRU(2, ttl=int(20e6))
        l[0] = 0
        l[1] = 1
        self.assertEqual(l[0], 0)
        self.assertEqual(l[1], 1)
        self.assertTrue(l.has_key(0))
        self.assertTrue(l.has_key(1))
        self.assertTrue(0 in l)
        self.assertTrue(1 in l)
        time.sleep(0.020)
        self.assertFalse(l.has_key(0))
        self.assertFalse(l.has_key(1))
        self.assertFalse(0 in l)
        self.assertFalse(1 in l)

    def test_set_with_ttl(self):
        l = TTLRU(2)
        l.set_with_ttl(0, 0, int(20e6))
        l.set_with_ttl(1, 1, int(80e6))
        self.assertTrue(0 in l)
        self.assertTrue(1 in l)
        time.sleep(0.01)  # approximately 0.01s
        self.assertTrue(0 in l)
        self.assertTrue(1 in l)
        time.sleep(0.01)  # approximately 0.02s
        self.assertTrue(0 not in l)
        self.assertTrue(1 in l)
        time.sleep(0.01)  # approximately 0.03s
        self.assertTrue(0 not in l)
        self.assertTrue(1 in l)        
        time.sleep(0.04)  # approximately 0.07s
        self.assertTrue(0 not in l)
        self.assertTrue(1 in l)   
        time.sleep(0.01)  # approximately 0.08s
        self.assertTrue(0 not in l)
        self.assertTrue(1 not in l)

    def test_peek_first_item_ttl(self):
        l = TTLRU(2)
        l.set_with_ttl(0, 0, int(80e6))
        l.set_with_ttl(1, 1, int(20e6))
        self.assertEqual(l.peek_first_item(), (1,1))
        self.assertEqual(l.peek_last_item(), (0,0))

        time.sleep(0.01)  # approximately 0.01s
        self.assertEqual(l.peek_first_item(), (1,1))

        time.sleep(0.01)  # approximately 0.02s
        self.assertEqual(l.peek_first_item(), (0,0))

        time.sleep(0.05)  # approximately 0.07s
        self.assertEqual(l.peek_first_item(), (0,0))

        time.sleep(0.01)  # approximately 0.08s
        self.assertEqual(l.peek_first_item(), None)

    def test_peek_last_item_ttl(self):
        l = TTLRU(2)
        l.set_with_ttl(0, 0, int(80e6))
        l.set_with_ttl(1, 1, int(20e6))
        self.assertEqual(l.peek_first_item(), (1,1))
        self.assertEqual(l.peek_last_item(), (0,0))

        time.sleep(0.01)  # approximately 0.01s
        self.assertEqual(l.peek_last_item(), (0,0))

        time.sleep(0.01)  # approximately 0.02s
        self.assertEqual(l.peek_last_item(), (0,0))

        time.sleep(0.05)  # approximately 0.07s
        self.assertEqual(l.peek_last_item(), (0,0))

        time.sleep(0.01)  # approximately 0.08s
        self.assertEqual(l.peek_first_item(), None)


        l.set_with_ttl(0, 0, int(10e6))
        l.set_with_ttl(1, 1, int(20e6))

        self.assertEqual(l.peek_last_item(), (0,0))
        time.sleep(0.01)  # approximately 0.01s
        self.assertEqual(l.peek_last_item(), (1,1))
        time.sleep(0.01)  # approximately 0.02s
        self.assertEqual(l.peek_first_item(), None)

    def test_no_ttl(self):
        l = TTLRU(2)
        l.set_with_ttl(0, 0, -1)
        l.set_with_ttl(1, 1, int(20e6))
        self.assertEqual(l.items(), [(1, 1), (0, 0)])
        time.sleep(0.02)  # approximately 0.02s
        self.assertEqual(l.items(), [(0, 0)])
        time.sleep(0.02)  # approximately 0.04s
        self.assertEqual(l.items(), [(0, 0)])
    
    def test_replace_key_with_ttl(self):
        l = TTLRU(2, ttl=int(20e6))
        l[1] = 1
        self.assertEqual(l.items(), [(1, 1)])
        time.sleep(0.01)  # approximately 0.01s
        self.assertEqual(l.items(), [(1, 1)])
        l[1] = 2
        self.assertEqual(l.items(), [(1, 2)])
        time.sleep(0.01)  # approximately 0.01s
        self.assertEqual(l.items(), [(1, 2)])
        time.sleep(0.01)  # approximately 0.03s
        self.assertEqual(l.items(), [])

    def test_ref_count(self):
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        l.get(1)
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        l.peek_first_item()
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        l.peek_last_item()
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        l.keys()
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        l[1] = 2
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        1 in l
        self.assertEqual(sys.getrefcount(x), 2)

        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        time.sleep(0.02)
        self.assertEqual(sys.getrefcount(x), 3)
        try:
            l[1]
        except:
            pass
        self.assertEqual(sys.getrefcount(x), 2)

        # ====================
        l = TTLRU(2,ttl=int(20e6))
        x = {1:2}
        self.assertEqual(sys.getrefcount(x), 2)
        l[1] = x
        self.assertEqual(sys.getrefcount(x), 3)
        l[1] = 2
        self.assertEqual(sys.getrefcount(x), 2)

if __name__ == '__main__':
    unittest.main()
