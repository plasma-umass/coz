#if !defined(CAUSAL_RUNTIME_SCOPE_H)
#define CAUSAL_RUNTIME_SCOPE_H

#include <set>
#include <string>

using std::set;
using std::string;

class scope {
public:
  /// Default constructor: no patterns
  scope() {}
  
  /// Single-pattern constructor
  scope(string pattern) {
    _patterns.insert(pattern);
  }
  
  /// Check if a name is in scope
  bool matches(string subject) {
    for(const string& pattern : _patterns) {
      if(wildcard_match(subject.begin(), subject.end(), pattern.begin(), pattern.end())) {
        return true;
      }
    }
    return false;
  }
  
private:
  /// Wildcard matching
  static bool wildcard_match(string::const_iterator subject,
                             string::const_iterator subject_end,
                             string::const_iterator pattern,
                             string::const_iterator pattern_end) {

    if((pattern == pattern_end) != (subject == subject_end)) {
      // If one but not both of the iterators have finished, match failed
      return false;
      
    } else if(pattern == pattern_end && subject == subject_end) {
      // If both iterators have finished, match succeeded
      return true;

    } else if(*pattern == '%') {
      // Try possible matches of the wildcard, starting with the longest possible match
      for(auto match_end = subject_end; match_end >= subject; match_end--) {
        if(wildcard_match(match_end, subject_end, pattern+1, pattern_end)) {
          return true;
        }
      }
      // No matches found. Abort
      return false;

    } else {
      // Walk through non-wildcard characters to match
      while(subject != subject_end && pattern != pattern_end && *pattern != '%') {
        // If the characters do not match, abort. Otherwise keep going.
        if(*pattern != *subject) {
          return false;
        } else {
          pattern++;
          subject++;
        }
      }

      // Recursive call to handle wildcard or termination cases
      return wildcard_match(subject, subject_end, pattern, pattern_end);
    }
  }
  
private:
  /// The set of patterns that define this scope
  set<string> _patterns;
};

#endif
