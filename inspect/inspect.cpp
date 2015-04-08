#include <cstdio>
#include <mutex>
#include <set>
#include <string>

#include "interval_dict.h"
#include "scope.h"

using std::mutex;
using std::set;
using std::string;

class inspector {
public:
  /// Initialize the inspector with a scope for binary objects and source files
  inspector(scope obj_scope = scope("MAIN"), scope src_scope = scope("%")) :
    _obj_scope(obj_scope), _src_scope(src_scope) {}
  
private:
  scope _obj_scope;
  scope _src_scope;
};

int main(int argc, char** argv) {
  printf("Hello World\n");
  
  inspector i;
  
  return 0;
}
