from setuptools import setup, Extension

module1 = Extension('ttlru',
                    sources = ['ttlru.c'])

setup (name = 'ttlru-dict',
       version = '1.0.1',
       description = 'An Dict like LRU container which also has ttl feature.',
       long_description = open('README.md').read(),
       long_description_content_type="text/markdown",
       author='Myrfy001',
       url='https://github.com/myrfy001/ttlru-dict',
       license='MIT',
       keywords='ttl, lru, dict, cache',
       ext_modules = [module1],
       classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Operating System :: POSIX',
        'Programming Language :: C',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: Implementation :: CPython',
        'Topic :: Software Development :: Libraries :: Python Modules',
        ],
)
