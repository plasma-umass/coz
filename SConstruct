# Set up the default build environment
env = Environment()
Export('env')

env.Replace(CXX='clang++')

# Set C++11 mode, generate position-independent code
env.Append(CCFLAGS='--std=c++11 -fPIC')

# Add include paths
env.Append(CPPPATH=['include', 
                    '#/deps/libelfin/dwarf',
                    '#/deps/libelfin/elf',
                    '#/deps/cppgoodies/include'])

# Add lib paths
env.Append(LIBPATH=['#/deps/libelfin/dwarf',
                    '#/deps/libelfin/elf'])

# Invoke scons in all building subdirectories
targets = env.SConscript(dirs=['lib', 'tools'])
