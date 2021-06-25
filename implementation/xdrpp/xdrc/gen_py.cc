

#include <sstream>
#include <utility>
#include <set>

#include "xdrc_internal.h"

using std::endl;




namespace {

indenter nl;
vec<string> scope;
vec<string> namespaces;
vec<string> nested_decl_names;

std::string c_typename_prefix = "c_";

std::string import_qualifier = "bleh";

std::set<string> generated_flattened_types;

std::unordered_map<string, string> xdr_type_map = {
  {"unsigned", "uint32_t"},
  {"int", "int32_t"},
  {"unsigned hyper", "uint64_t"},
  {"hyper", "int64_t"},
  {"opaque", "uint8_t"},
  {"float", "float"},
  {"uint8_t", "uint8_t"},
  {"char", "char"}

};

std::set<string> stdint_types = {
  "uint8_t",
  "int32_t",
  "uint32_t",
  "int64_t",
  "uint64_t",
};

std::unordered_map<string, string> xdr_native_types = {
  {"unsigned", "uint32_t_wrapper"},
  {"int", "int32_t_wrapper"},
  {"unsigned hyper", "uint64_t_wrapper"},
  {"hyper", "int64_t_wrapper"},
  {"float", "float_wrapper"},
  {"uint8_t", "uint8_t_wrapper"},
  {"char", "char_wrapper"}
};

std::unordered_map<string, string> py_base_types = {
  {"unsigned", "int"},
  {"int", "int"},
  {"unsigned hyper", "int"},
  {"hyper", "int"},
  {"float", "float"},
  {"uint8_t", "int"},
  {"char", "int"},
};

//std::unordered_map<string, string> xdr_float_types = {
 // {"float", "float_wrapper"},
//};

std::unordered_map<string, string> constants_map;

std::string export_modules;

vec<string> util_method_classnames;

string
map_type(const string &s)
{
  auto t = xdr_type_map.find(s);
  if (t == xdr_type_map.end())
    return s;
  return t->second;
}


string py_nested_decl_name()
{
  auto out = string("");
  for (auto& scope : nested_decl_names) {
      out += scope + string("_");
  }
  return out;
}
//bool is_int_type(const string &s) {
//  return xdr_int_types.find(s) != xdr_int_types.end();
//}

//bool is_float_type(const string& s) {
//  return xdr_float_types.find(s) != xdr_float_types.end();
//}



bool is_native_type(const string& raw_typename) {
  return xdr_native_types.find(raw_typename) != xdr_native_types.end();//is_int_type(raw_typename) || is_float_type(raw_typename);
}

bool is_native_type(const rpc_decl& decl) {
  if (decl.qual != rpc_decl::SCALAR) {
    return false;
  }
  return is_native_type(decl.type);
}

string xdr_native_typename(const string& classname, const string& filename) {
  return xdr_native_types.at(classname) + "_" + filename;
}

void gen_null_check_pyx(std::ostream& os, const string& var = "self") {
  os << nl << var << ".null_check()";
}

void gen_general_from_ptr_pyx(std::ostream& os, const string& classname, const string& qualified_name) {

  auto clobbered_classname = py_nested_decl_name() + classname;
  os << nl << "@staticmethod";

  os << nl << "cdef " << clobbered_classname << " from_ptr(" << qualified_name << "* ptr, bool ownership = False):";
  ++nl;
  os << nl << "cdef " << clobbered_classname << " output = " << clobbered_classname << ".__new__(" << clobbered_classname << ")"
     << nl << "output.ptr = ptr"
     << nl << "output.ownership = ownership"
     << nl << "return output" << endl;
  --nl;
}

void gen_general_from_ptr_pxd(std::ostream& os, const string& classname, const string& qualified_name) {
//  auto qualified_name = c_typename_prefix + py_nested_decl_name() + classname;
  auto clobbered_classname = py_nested_decl_name() + classname;

  os << nl << "@staticmethod";
  os << nl << "cdef " << clobbered_classname << " from_ptr(" << qualified_name << "* ptr, bool ownership = *)";
}


void gen_base_native_from_ptr_method_pyx(std::ostream& os, const string& classname, const string &filename) {
  os << nl << "@staticmethod";
  os << nl << "cdef " << xdr_native_typename(classname, filename) << " from_ptr(" << xdr_type_map.at(classname) + "* ptr, bool ownership = False):";
  ++nl;
    os << nl << "return " << xdr_native_typename(classname, filename) << "(deref(ptr))";
  --nl;

  os << nl << "cdef void null_check(" << xdr_native_typename(classname, filename)<< " self):";
  ++nl;
    os << nl << "pass";
  --nl;

  if (classname == "char") {
    os << nl << "cdef char get_xdr(" << xdr_native_typename(classname, filename) << " self):";
    ++nl;
      os << nl << "return <char> (self)";
    --nl;
  } else {
    os << nl << "cdef " <<  xdr_type_map.at(classname) << " get_xdr(" << xdr_native_typename(classname, filename) << " self):";
    ++nl;
      os << nl << "return self";
    --nl;
  }
}

void gen_base_native_from_ptr_method_pxd(std::ostream& os, const string& classname, const string &filename) {
  os << nl << "@staticmethod";
  os << nl << "cdef " << xdr_native_typename(classname, filename) << " from_ptr(" << xdr_type_map.at(classname) << "* ptr, bool ownership = *)";
  os << nl << "cdef void null_check(" << xdr_native_typename(classname, filename)<< " self)";
  os << nl << "cdef " << xdr_type_map.at(classname) << " get_xdr(" << xdr_native_typename(classname, filename) << " self)";
}


void gen_base_extensionclass_from_ptr_method_pyx(std::ostream& os, const string& new_classname, const string& extends_classname, const string& file_prefix) {

  os << nl << "@staticmethod";
  os << nl << "cdef " << new_classname << " from_ptr(" << c_typename_prefix + new_classname + "* ptr, bool ownership = False):";
  ++nl;
    //os << nl << "cdef " << extends_classname " * base = " << extends_classname << ".from_ptr(ptr, ownership)"
     //  << nl << ""
    os << nl << "return "<< new_classname << "(" << extends_classname << ".from_ptr(ptr, ownership))" << endl;
  --nl;
}

void gen_base_extensionclass_from_ptr_method_pxd(std::ostream& os, const string& new_classname, const string& extends_classname, const string& file_prefix) {
  os << nl << "@staticmethod";
  os << nl << "cdef " << new_classname << " from_ptr(" << c_typename_prefix << new_classname + "* ptr, bool ownership = *)";
}


void gen_native_decl_pyx(std::ostream& os, const string& classname, const string& filename) {
  os << nl << "cdef class " << xdr_native_typename(classname, filename) << "(" << py_base_types.at(classname) << "):";
  ++nl;
    gen_base_native_from_ptr_method_pyx(os, classname, filename);
  --nl;
  os << endl;
}

void gen_native_decl_pxd(std::ostream& os, const string& classname, const string& filename) {
  os << nl << "cdef class " << xdr_native_typename(classname, filename) << "(" << py_base_types.at(classname) << "):";
  ++nl;
    gen_base_native_from_ptr_method_pxd(os, classname, filename);
  --nl;
  os << endl;
}

string
cur_ns()
{
  string out;
  for (const auto &ns : namespaces) {
    if (ns != "") {
      out += string("::") + ns;
    }
  }
  return out.empty() ? "::" : out;
}

/*
string cur_flat_ns() {
  string out;
  for (const auto &ns : namespaces)
    out += string("_") + ns;
  return out.empty() ? "_" : out;
} */


string flatten_namespaces(string input) {
  while (true) {
    auto idx = input.find("::");
    if (idx == string::npos) {
      return input;
    }
    if (idx == 0) {
      input = input.substr(2);
    } else {
      input.replace(idx, 2, "_");
    }
  }
}

string
xdr_type(const rpc_decl &d)
{
  string type = map_type(d.type);

  auto bound_name = string(d.bound);
  while (bound_name.find("::") == 0) {
    bound_name = bound_name.substr(2);
  }

  if (constants_map.find(bound_name) != constants_map.end()) {
    string ns = cur_ns();
    if (ns != "::") {
      bound_name = cur_ns() + string("::") + bound_name;
    } else {
      bound_name = string("::") + bound_name;
    }
  }

  if (d.type == "string")
    return string("xdr::xstring<") + bound_name + ">";
  if (d.type == "opaque")
    switch (d.qual) {
    case rpc_decl::ARRAY:
      return string("xdr::opaque_array<") + bound_name + ">";
    case rpc_decl::VEC:
      return string("xdr::opaque_vec<") + bound_name + ">";
    default:
      assert(!"bad opaque qualifier");
    }

  auto type_name = type;

  while (type_name.find("::") == 0) {
    type_name = type_name.substr(2);
  }

  if (!is_native_type(type_name)) {
    auto ns = cur_ns();
    if (ns != "::") {
      type_name = ns + "::" + type_name;
    } else {
      type_name = "::" + type_name;
    }
  }

  switch (d.qual) {
  case rpc_decl::PTR:
    return string("xdr::pointer<") + type_name + ">";
  case rpc_decl::ARRAY:
    return string("xdr::xarray<") + type_name + "," + bound_name + ">";
  case rpc_decl::VEC:
    return string("xdr::xvector<") + type_name +
      (d.bound.empty() ? ">" : string(",") + bound_name + ">");
  default:
    return type;
  }
}


string 
cppclass_type_string(const string& t) {
  //return t;
  auto out = cur_ns();
  if (namespaces.empty()) {
    out = string("");
  }
  if (nested_decl_names.size()) {
    for (auto &n : nested_decl_names) {
      out += string("::") + n;
    }
  }
  return out + "::" + t;
}

bool needs_flattened_declaration(const rpc_decl &d) {
  string type = map_type(d.type);
  if (type == "string") return true;
  if (d.type == "opaque") return true;
  switch(d.qual) {
    case rpc_decl::ARRAY:
    case rpc_decl::VEC:
    case rpc_decl::PTR:
      return true;
    default:
      return false;
  }
}

/*
bool needs_subtype_declaration(const rpc_decl &d) {
  switch (d.ts_which) {
    case rpc_decl::TS_ENUM:
    case rpc_decl::TS_STRUCT:
    case rpc_decl::TS_UNION:
      return true;
    default:
      return false;
  }
} */

string py_qualifier(const string& type) {
  string out = string("");

  if (is_native_type(type)) {
    return map_type(type);
  }

  if (stdint_types.find(type) != stdint_types.end()) {
    return type;
  }

  string raw_name = map_type(type);

  while(true) {
    auto idx = raw_name.find("::");
    if (idx == string::npos) {
      break;
    }

    string containerclass = raw_name.substr(0, idx);
    raw_name = raw_name.substr(idx+2);
    if (containerclass == "") continue;
    out += c_typename_prefix + containerclass + ".";
  }
  return out + c_typename_prefix + raw_name;
}

string py_type(const rpc_decl &d, const string& file_prefix)
{
  string type = map_type(d.type);

  auto filename = strip_directory(file_prefix);

  if (d.type == "string")
    return string("xdr_xstring_") + d.bound + "_"+ filename;
  if (d.type == "opaque")
    switch (d.qual) {
    case rpc_decl::ARRAY:
      return string("xdr_opaque_array_") + d.bound + "_" + filename;
    case rpc_decl::VEC:
      return string("xdr_opaque_vec_") + d.bound + "_" + filename;
    default:
      assert(!"bad opaque qualifier");
    }

  type = flatten_namespaces(type);

  switch (d.qual) {
  case rpc_decl::PTR:
    return string("xdr_pointer_") + type + "_" + filename;
  case rpc_decl::ARRAY:
    return string("xdr_xarray_") + type + "_" + d.bound + "_" + filename;
  case rpc_decl::VEC:
    return string("xdr_xvector_") + type +
      (d.bound.empty() ? "" : string("_") + d.bound) + "_" + filename;
  default:
    return type;
  }
}

string contained_type(const rpc_decl &d)
{
  if (d.type == "opaque") {
    switch(d.qual) {
      case rpc_decl::ARRAY:
      case rpc_decl::VEC:
        return string("uint8_t");
      default:
        assert(!"bad opaque qualiifier");
    }
  }
  if (d.type == "string") {
    return "char";
//    throw std::runtime_error("shouldn't be getting string from here!");
  }
  switch(d.qual) {
    case rpc_decl::PTR:
    case rpc_decl::ARRAY:
    case rpc_decl::VEC:
      return map_type(d.type);
    default:
      assert(!"not a container type");
  }
}

string py_typename_native(const string& raw_name) {
  if (is_native_type(raw_name)) {
    return xdr_native_types.at(raw_name);
  }
  return raw_name;
}

void gen_native_type_wrapper_pyx(std::ostream& os, const string& raw_typename, const string& file_prefix) {

  if (generated_flattened_types.find(raw_typename) == generated_flattened_types.end()) {
    generated_flattened_types.insert(raw_typename);
    if (is_native_type(raw_typename)) {
      gen_native_decl_pyx(os, raw_typename, strip_directory(file_prefix));
    }
  }
}

void gen_native_type_wrapper_pxd(std::ostream& os, const string& raw_typename, const string& file_prefix) {
  if (generated_flattened_types.find(raw_typename) == generated_flattened_types.end()) {
    generated_flattened_types.insert(raw_typename);
    if (is_native_type(raw_typename)) {
      gen_native_decl_pxd(os, raw_typename, strip_directory(file_prefix));
    }
  }
}


/*
string
cur_scope()
{
  string out;
  if (!namespaces.empty())
    out = cur_ns();
  if (!scope.empty()) {
    out += "::";
    out += scope.back();
  }
  return out;
} */

string xdr_nested_decl_name() {
  auto out = string("");
  for (auto& scope : nested_decl_names) {
    if (out.empty()) {
      out += scope;
    } else {
      out += string("::") + scope;
    }
  }
  return out;
}

/*
string cpp_syntax_nested_decl_name() {
  auto out = string("");
  for (auto& scope : nested_decl_names) {
      out += scope + string(".");
  }
  return out;
}
*/

string py_nested_decl_name_for_subtype_import() {
  auto out = py_nested_decl_name();
  out.pop_back();
  return out;
}

void 
gen_pointer_class_pyx(
  std::ostream& os, 
  const std::string& classname, 
  const std::string& xdr_typename = "", 
  bool load_methods = true,
  const std::string& new_ctor_args = "", 
  const std::string& new_ctor_invokes = "") {
  
  auto clobber_prefix = py_nested_decl_name();

  auto qualified_name = xdr_typename;
  if (qualified_name == "") {
      qualified_name = c_typename_prefix + clobber_prefix + classname;
  }

  auto clobbered_classname = clobber_prefix + classname;
  os << nl << "cdef class " << clobbered_classname << ":";
  ++nl;

  os << nl << "def __init__(self, x):";
  ++nl;
    os << nl << "self.set_self_as_ref(x)";
  --nl;

  if (load_methods) {
    os << nl << "cdef set_self_as_copy(" << clobbered_classname << " self, " << clobbered_classname << " other):";
    ++nl;
      gen_null_check_pyx(os, "other");
      os << nl << "self.ptr = new " << qualified_name << "()"
        << nl << "if self.ptr is NULL:";
        ++nl;
          os << nl << "raise MemoryError";
        --nl;
        os << nl << "self.ownership = True"
           << nl << "self.ptr[0] = (other.get_xdr())";
    --nl;
  }

   os << nl << "cdef set_self_as_ref(" << clobbered_classname << " self, " << clobbered_classname << " other):";
  ++nl;
    gen_null_check_pyx(os, "other");
    os << nl << "self.ptr = other.ptr";
  --nl;
    //something like a move ctor
    //self.ptr = x.ptr
    //self.ownership = x.ownership
    //x.ownership = False
    
    //x.ptr = NULL

 // os << nl << "cdef " << qualified_name << "* ptr"
  //   << nl << "cdef bool ownership" << endl;
  
  os << nl << "def __cinit__(self):";
  ++nl;
  os << nl << "self.ownership = False" << endl;
  --nl;

  os << nl << "def __dealloc__(self):";
  ++nl;
  os << nl << "if (self.ptr is not NULL) and self.ownership:";
  ++nl;
  os << nl << "del self.ptr"
     << nl << "self.ownership = False" << endl;
  --nl;
  --nl;

  os << nl << "@staticmethod"
     << nl << "cdef " << clobbered_classname << " new_ctor(" << new_ctor_args << "):";
  ++nl;
  os << nl << "cdef " << qualified_name << "* ptr = new " << qualified_name << "(" << new_ctor_invokes << ")"
     << nl << "if ptr is NULL:";
  ++nl;
  os << nl << "raise MemoryError";
  --nl;
  os << nl << "return " << clobbered_classname << ".from_ptr(ptr, ownership = True)" <<endl;
  --nl;

  if (load_methods) {
    os << nl << "@staticmethod"
       << nl << "cdef " << clobbered_classname << " copy_ctor(" << clobbered_classname << " other):";
    ++nl;
    os << nl << "cdef " << qualified_name << "* ptr = new " << qualified_name << "(deref(other.ptr))"
       << nl << "if ptr is NULL:";
    ++nl;
    os << nl << "raise MemoryError";
    --nl;
    os << nl << "return " << clobbered_classname << ".from_ptr(ptr, ownership = True)" <<endl;
    --nl;
  }

  os << nl << "@staticmethod"
     << nl << "cdef " << clobbered_classname << " make_ref(" << clobbered_classname << " other):";
     ++nl;
     os << nl << "return " << clobbered_classname << ".from_ptr(other.ptr, ownership = False)" <<endl;
     --nl;
  

  if (load_methods) {

    os << nl << "cdef int _load_from_file(" << clobbered_classname << " self, string filename) except -1:";
      ++nl;
      os << nl << "return load_xdr_from_file(deref(self.ptr), filename.c_str())";
      --nl;
    os << nl;

    os << nl << "def load_from_file(self, filename):";
    ++nl;
      os << nl << "self.null_check()";
      os << nl << "self._load_from_file(filename)";
    --nl;
    os << nl;

    os << nl << "cdef int _save_to_file(" << clobbered_classname << " self, string filename) except -1:";
      ++nl;
      os << nl << "return save_xdr_to_file(deref(self.ptr), filename.c_str())";
      --nl;
    os << nl;

    os << nl << "def save_to_file(self, filename):";
    ++nl;
      os << nl << "self.null_check()";
      os << nl << "self._save_to_file(filename)";
    --nl;
    os << nl;
  }

  os << nl << "@staticmethod"
     << nl << "def reference(other):";
  ++nl;
  os << nl << "return " << clobbered_classname << ".make_ref(other)" <<endl;
  --nl;

  if (load_methods) {
    os << nl << "@staticmethod"
       << nl << "def copy(other):";
    ++nl;
    os << nl << "return " << clobbered_classname << ".copy_ctor(other)" <<endl;
    --nl;
  }

  os << nl << "@staticmethod"
     << nl << "def new(" << new_ctor_invokes << "):";
  ++nl;
  os << nl << "return " << clobbered_classname << ".new_ctor(" << new_ctor_invokes << ")" <<endl;
  --nl;

  os << nl << "def null_check(self):";
  ++nl;
    os << nl << "if self.ptr is NULL:";
    ++nl;
      os << nl << "raise MemoryError(\"null pointer access\")";
    --nl;
  --nl;

  if (load_methods) {
    os << nl << "cdef " << qualified_name << " get_xdr(" << clobbered_classname << " self):";
    ++nl;
      os << nl << "return deref(self.ptr)";
    --nl;
  }

 // os << nl << "cdef bool equals(" << clobbered_classname << " self, " << clobbered_classname << " other):";
 // ++nl;
 //   os << nl << "return deref(self.ptr) == deref(other.ptr)";
 // --nl;

  /*os << nl << "def __eq__(self, other):";
  ++nl;
    gen_null_check_pyx();
    gen_null_check_pyx("other");
    os << nl << "return self.equals(other)";
  --nl;

  os << nl << "def __ne__(self, other):";
  ++nl;
    os << nl << "return not self.__eq__(other)";
  --nl;*/

  gen_general_from_ptr_pyx(os, classname, qualified_name);
}

void gen_pointer_class_pxd(std::ostream& os, const string& classname, const string& xdr_typename = "", bool load_methods = true, const string& new_ctor_args = "") {
  auto clobber_prefix = py_nested_decl_name();

  auto qualified_name = xdr_typename;
  if (qualified_name == "") {
      qualified_name = c_typename_prefix + clobber_prefix + classname;
  }

  auto clobbered_classname = clobber_prefix + classname;

  os << nl << "cdef class " << clobbered_classname << ":";
  ++nl;


  os << nl << "cdef " << qualified_name << "* ptr"
     << nl << "cdef bool ownership" << endl;
  
  if (load_methods)
    os << nl << "cdef set_self_as_copy(" << clobbered_classname << " self, " << clobbered_classname << " other)";
  os << nl << "cdef set_self_as_ref(" << clobbered_classname << " self, " << clobbered_classname << " other)";

  os << nl << "@staticmethod"
     << nl << "cdef " << clobbered_classname << " new_ctor(" << new_ctor_args << ")";


  if (load_methods) {
    os << nl << "@staticmethod"
       << nl << "cdef " << clobbered_classname << " copy_ctor(" << clobbered_classname << " other)";
  }
  os << nl << "@staticmethod"
     << nl << "cdef " << clobbered_classname << " make_ref(" << clobbered_classname << " other)";
  
  if (load_methods) {
    os << nl << "cdef int _load_from_file(" << clobbered_classname << " self, string filename) except -1";

    os << nl << "cdef int _save_to_file(" << clobbered_classname << " self, string filename) except -1";
  }
  //os << nl << "cdef void null_check(" << clobbered_classname << " self) except -1";
  if (load_methods)
    os << nl << "cdef " << qualified_name << " get_xdr(" << clobbered_classname << " self)";

 // os << nl << "cdef bool equals(" << clobbered_classname << " self, " << clobbered_classname << " other)";

  gen_general_from_ptr_pxd(os, classname, qualified_name);
}




void add_to_module_export(const std::string& name) {
  if (export_modules == "") {
    export_modules = string("[\"") + name + "\"";
  } else {
    export_modules += ",\n\"" + name + "\"";
  }
}

void end_gen_pointer_class(std::ostream& os) {
  --nl;
  os << endl;
  os << endl;
}


void gen_vector_methods_pyx(std::ostream& os, const std::string& classname, const std::string& contained_type, const std::string& file_prefix) {

  os << nl << "cdef _size(self):";
  ++nl;
    gen_null_check_pyx(os);
    os << nl << "return deref(self.ptr).size()" << endl;
  --nl;

  os << nl << "def size(self):" << endl;
  ++nl;
    os << nl << "return self._size()" << endl;
  --nl;

  os << nl << "cdef set(self, uint32_t key, " << contained_type << " value):";
  ++nl;
    gen_null_check_pyx(os);
    gen_null_check_pyx(os, "value");
      os << nl << "if key >= deref(self.ptr).size() or key < 0:";
        ++nl;
          os << nl << "raise IndexError(\"invalid index\")";
        --nl;

      os << nl << "deref(self.ptr)[key] = value.get_xdr()";
  --nl;

  os << nl << "def __setitem__(self, key, value):";
  ++nl;
    os << nl << "self.set(key, " << contained_type << "(value))";
  --nl;

  os << nl << "def __getitem__(self, key):";
  ++nl;
    gen_null_check_pyx(os);
    os << nl << "if key >= deref(self.ptr).size() or key < 0:";
    ++nl;
      os << nl << "raise IndexError(\"invalid index\")";
    --nl;
    os << nl << "return " << contained_type << ".from_ptr(addr(deref(self.ptr)[key]), ownership = False)" <<endl;
  --nl;

  os << nl << "cdef _push_back(self, " << contained_type << " element):" ;
  ++ nl;
    os << nl << "deref(self.ptr).push_back(element.get_xdr())";
  --nl;

  os << nl << "def push_back(self, elt):";
  ++nl;
    os << nl << "self._push_back(" << contained_type<< "(elt))";
  --nl;

}

void gen_vector_methods_pxd(std::ostream& os, const std::string& classname, const std::string& contained_type, const std::string& file_prefix) {

  os << nl << "cdef _size(self)";

  auto contained_guarded_typename = contained_type;
  if (is_native_type(contained_type)) {
    contained_guarded_typename = py_typename_native(contained_type);
  }

  os << nl << "cdef set(self, uint32_t key, " << contained_guarded_typename << " value)";
  os << nl << "cdef _push_back(self, " << contained_guarded_typename << " element)" ;


}

void gen_array_methods_pyx(std::ostream& os, const std::string& classname, const std::string& contained_type, const std::string& file_prefix) {
  auto qualified_name = c_typename_prefix + classname;

  os << nl << "@staticmethod"
     << nl << "cdef _size():";
  ++nl;
  os << nl << "return " << qualified_name << ".size()" << endl;
  --nl;

  os << nl << "@staticmethod"
     << nl << "def size():";
  ++nl;
    os << nl << "return " << classname << "._size()";
  --nl;

  os << nl << "cdef set(self, uint32_t key, " << contained_type << " value):";
  ++nl;
    gen_null_check_pyx(os);
    gen_null_check_pyx(os, "value");
      os << nl << "if key >= " << classname << "._size() or key < 0:";
        ++nl;
          os << nl << "raise IndexError(\"invalid index\")";
        --nl;

      os << nl << "deref(self.ptr)[key] = value.get_xdr()";
  --nl;

  os << nl << "def __setitem__(self, key, value):";
  ++nl;
    os << nl << "self.set(key, " << contained_type << "(value))";
  --nl;

  os << nl << "def __getitem__(self, key):";
  ++nl;
    gen_null_check_pyx(os);
    os << nl << "if key >= " << classname << ".size() or key < 0:";
    ++nl;
      os << nl << "raise IndexError(\"array index out of bounds\")";
    --nl;
    os << nl << "return deref(self.ptr)[key]" <<endl;
  --nl;

}

void gen_array_methods_pxd(std::ostream& os, const std::string& classname, const std::string& contained_type, const std::string& file_prefix) {

  os << nl << "@staticmethod"
     << nl << "cdef _size()";

  auto contained_guarded_typename = contained_type;
  if (is_native_type(contained_type)) {
    contained_guarded_typename = py_typename_native(contained_type);
  }


  os << nl << "cdef set(self, uint32_t key, " << contained_guarded_typename << " value)";

}




void gen_extern_cdef(std::ostream& os, const std::string& file_prefix) {
  os << nl << "cdef extern from \"" << file_prefix << ".h\"";
  if (namespaces.size()) {
    os << " namespace \"" << cur_ns() << "\"";
  }
  os << ":";
}

void gen_helper_assignment(std::ostream& os, const std::string& name) {
    return;
    os << nl << "\"\"\""
      <<nl << "void helper_assignment(" << name <<"& dst, const " << name << "& src) {"
       << nl << "  src = dst;"
    << nl << "}"
    << nl << "\"\"\"";
}

void gen_ctors_pxdi(std::ostream& os, const std::string& name) {

  os << nl << name << "() except +" 
     << nl << name << "(const " << name << "&) except +"
     << nl << "void operator=(const " << name << "&)";
   //  << nl << "bool operator==(const " << name << "&)";  // operator== doesn't exist in xdr files.
}

void gen_array_pxdi(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  gen_extern_cdef(os, file_prefix);
  ++nl;

  auto py_name = py_type(d, file_prefix);
  auto xdr_name = xdr_type(d);
  auto contained_name = py_qualifier(contained_type(d));

  gen_helper_assignment(os, c_typename_prefix + py_name);

  os << nl << "cdef cppclass " <<c_typename_prefix << py_name << " \"" << xdr_name << "\":";
  ++nl;
  gen_ctors_pxdi(os, c_typename_prefix + py_name);
  os << nl << contained_name << "& operator[](int)"
    << nl << "@staticmethod"
    << nl << "uint32_t size()" << endl;
    --nl;

  --nl;

  util_method_classnames.push_back(c_typename_prefix + py_name);
}

void gen_vector_pxdi(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  gen_extern_cdef(os, file_prefix);
  ++nl;
    auto py_name = py_type(d, file_prefix);
    auto xdr_name = xdr_type(d);
    auto contained_name = py_qualifier(contained_type(d));

    gen_helper_assignment(os, c_typename_prefix + py_name);

    os << nl << "cdef cppclass " << c_typename_prefix << py_name << " \"" << xdr_name << "\":";
    ++nl;
      gen_ctors_pxdi(os, c_typename_prefix + py_name);
      os << nl << contained_name << "& operator[](int)"
         << nl << "void push_back(" << contained_name << ") except +"
         << nl << "void check_size(size_t n) except +"
         << nl << "size_t size()"
         << nl << "erase(int)" << endl;
    --nl;
  --nl;

  util_method_classnames.push_back(c_typename_prefix + py_name);
}

/*
void gen_string_pxdi(std::ostream& os, const rpc_decl& d) {
  gen_extern_cdef(os, file_prefix);

  ++nl;
    auto py_name = py_type(d, file_prefix);
    auto xdr_name = xdr_type(d);
    auto contained_name = "char";

    gen_helper_assignment(os, c_typename_prefix + py_name);

    os << nl << "cdef cppclass " << c_typename_prefix << py_name << " \"" << xdr_name << "\":";
    ++nl;
      gen_ctors_pxdi(os, c_typename_prefix + py_name);
      os << nl << contained_name << "& operator[](int)"
         << nl << "void push_back(" << contained_name << ") except +"
         << nl << "void check_size(size_t n) except +"
         << nl << "size_t size()"
         << nl << "erase(int)" << endl;
    --nl;
  --nl;
}
*/

void gen_vector_pyx(std::ostream& os, const rpc_decl&d, const std::string& file_prefix) {
  gen_pointer_class_pyx(os, py_type(d, file_prefix));

  auto contained_typename = contained_type(d);
  if (is_native_type(contained_typename)) {
    contained_typename = py_typename_native(contained_typename) + "_" + strip_directory(file_prefix);
  }

  gen_vector_methods_pyx(os, py_type(d, file_prefix), contained_typename, file_prefix);
  end_gen_pointer_class(os);
}

void gen_array_pyx(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  gen_pointer_class_pyx(os, py_type(d, file_prefix));

  auto contained_typename = contained_type(d);
  if (is_native_type(contained_typename)) {
    contained_typename = py_typename_native(contained_typename) + "_" + strip_directory(file_prefix);
  }

  gen_array_methods_pyx(os, py_type(d, file_prefix), contained_typename, file_prefix);
  end_gen_pointer_class(os);
}

void gen_vector_pxd(std::ostream& os, const rpc_decl&d, const std::string& file_prefix) {
  gen_pointer_class_pxd(os, py_type(d, file_prefix));

  auto contained_typename = contained_type(d);
  if (is_native_type(contained_typename)) {
    contained_typename = py_typename_native(contained_typename) + "_" + strip_directory(file_prefix);
  }

  gen_vector_methods_pxd(os, py_type(d, file_prefix), contained_typename, file_prefix);
  end_gen_pointer_class(os);
}

void gen_array_pxd(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  gen_pointer_class_pxd(os, py_type(d, file_prefix));

  auto contained_typename = contained_type(d);
  if (is_native_type(contained_typename)) {
    contained_typename = py_typename_native(contained_typename) + "_" + strip_directory(file_prefix);
  }

  gen_array_methods_pxd(os, py_type(d, file_prefix), contained_typename, file_prefix);
  end_gen_pointer_class(os);
}


void gen_flattened_decl_pyx(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  if (generated_flattened_types.find(py_type(d, file_prefix))== generated_flattened_types.end()) {
    generated_flattened_types.insert(py_type(d, file_prefix));
    switch(d.qual) {
      case rpc_decl::ARRAY:
        gen_array_pyx(os, d, file_prefix);
        break;
      case rpc_decl::VEC:
        gen_vector_pyx(os, d, file_prefix);
        break;
      default:
        //nothing to do, need default case to suppress warning
        break;
    }
  }
}

void gen_flattened_decl_pxd(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
  if (generated_flattened_types.find(py_type(decl, file_prefix)) == generated_flattened_types.end()) {
    generated_flattened_types.insert(py_type(decl, file_prefix));
    switch(decl.qual) {
      case rpc_decl::ARRAY:
        gen_array_pxd(os, decl, file_prefix);
        break;
      case rpc_decl::VEC:
        gen_vector_pxd(os, decl, file_prefix);
        break;
      default:
        //nothing to do
        break;
    }
  }
}

void gen_flattened_decl_pxdi(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
  if (generated_flattened_types.find(py_type(decl, file_prefix))== generated_flattened_types.end()) {
    generated_flattened_types.insert(py_type(decl, file_prefix));
    switch(decl.qual) {
      case rpc_decl::ARRAY:
        gen_array_pxdi(os, decl, file_prefix);
        break;
      case rpc_decl::VEC:

        //if (decl.type == "string") {
        //  gen_string_pxdi(os, decl);
        //}
        //else {
          gen_vector_pxdi(os, decl, file_prefix);
        //}
        break;
      default:
        //nothing to do
        break;
    }
  }
}


void gen_enum_pxdi(std::ostream& os, const rpc_enum& e, const std::string& file_prefix) {
  gen_extern_cdef(os, file_prefix);
  ++nl;

  string scoped_py_name = py_nested_decl_name() + e.id;

  string qualified_name = xdr_nested_decl_name() + e.id;
  if (namespaces.size()) {
    qualified_name = cur_ns() + "::" + qualified_name;
  }

  os << nl << "cdef enum " << c_typename_prefix << e.id << " \"" <<qualified_name<< "\":";

  ++nl;
  for (const auto &tag : e.tags) {
    os << nl << c_typename_prefix << tag.id << " \"" << qualified_name << "::" << tag.id << "\"" << endl;
  }
  --nl;
  --nl;
}

void gen_enum_pyx(std::ostream& os, const rpc_enum& e, const std::string& file_prefix) {

  os << nl << "cdef class " << e.id << ":";
  ++nl;

  os << nl
     << nl << "def __init__(self, value):";
     ++nl;
     os << nl << "self.underlying = _"<<e.id<<"(value)" <<endl; // does the python enum validity checking
     --nl;

  for (const auto &tag : e.tags) {
    os << nl << tag.id << " = _" << e.id << "." << tag.id;
  }

  os << nl << "def __repr__(self):";
  ++nl;
    os << nl << "return _"<< e.id << "(self.underlying).name" << endl;
  --nl;

  os << nl << "@staticmethod"
     << nl << "cdef " << e.id << " from_ptr(" << c_typename_prefix << e.id << "* ptr, bool ownership = False):";
     ++nl;
     os << nl << "return " << e.id << "(deref(ptr))";
     --nl;

    os << nl << "cdef null_check(self):";
    ++nl;
      os << nl << "pass";
    --nl;

    os << nl << "cdef " << c_typename_prefix << e.id << " get_xdr(self):";
    ++nl;

      os << nl << "value_map = {";
      ++nl;
        for (const auto& tag : e.tags) {
          os << nl << "_" << e.id << "." << tag.id << " : " << c_typename_prefix << e.id << "." << c_typename_prefix <<  tag.id << ",";
        }
      os << nl << "}";
      --nl;
      os << nl << "return value_map[self.underlying]";
    --nl;
  --nl;
  os << endl;

  add_to_module_export(e.id);
}

void gen_enum_pxd(std::ostream& os, const rpc_enum& e, const std::string& file_prefix) {
  os << nl << "cpdef enum _" << e.id << ":";
  ++nl;
  for (const auto &tag : e.tags) {
    os << nl << tag.id << " = " << c_typename_prefix << e.id << "." << c_typename_prefix << tag.id;
  }
  --nl;
  os << endl;

  os << nl << "cdef class " << e.id << ":";
  ++nl;

    os << nl << "cdef _" << e.id << " underlying";

     os << nl << "@staticmethod"
        << nl << "cdef " << e.id << " from_ptr(" << c_typename_prefix << e.id << "* ptr, bool ownership = *)";

    os << nl << "cdef null_check(self)";

    os << nl << "cdef " << c_typename_prefix << e.id << " get_xdr(self)";

  os << endl;

  --nl;
}

void gen_var_access_methods_pyx(std::ostream& os, const std::string& mainclass, const rpc_decl& d, const std::string& clobber_prefix, const std::string& file_prefix) {

  auto obj_type = py_type(d, file_prefix);

  if (is_native_type(d)) { //TODO was d.type
    obj_type = py_typename_native(d.type) + "_" + strip_directory(file_prefix);
  }

 // os << nl << "cdef " << obj_type << " make_ref_" << d.id << "():";
   //  ++nl;
     //os << nl << "return " << obj_type << ".from_ptr("

  obj_type = clobber_prefix + obj_type;

  os << nl << "@property"
     << nl << "def " << d.id << "(self):";
     ++nl; 
     gen_null_check_pyx(os);
    // os << nl << "if issubclass(" << obj_type << ", int):";
     //++nl;
     //   os << nl << "return deref(self.ptr)." << d.id;
     //--nl;
     os << nl << "return " << obj_type << ".from_ptr(addr(deref(self.ptr)." << d.id << "), ownership = False)";
     --nl;
  os << nl;

  os << nl << "cdef set_" << d.id << "(" << mainclass << " self, " << obj_type << " other):";
  ++nl;
    gen_null_check_pyx(os);
    gen_null_check_pyx(os, "other");

    os << nl << "deref(self.ptr)." << d.id << " = other.get_xdr()";
  --nl;

  os << nl << "@" << d.id << ".setter";
  os << nl << "def " << d.id << "(self, value):";
  ++nl;
    os << nl << "self.set_" << d.id << "(" << obj_type << "(value))";
  --nl;

}

void gen_var_access_methods_pxd(std::ostream& os, const std::string& mainclass, const rpc_decl& d,  const std::string& clobber_prefix, const std::string& file_prefix) {

  auto obj_type = py_type(d, file_prefix);

  if (is_native_type(d)) { //TODO was d.type
    obj_type = py_typename_native(d.type) + "_" + strip_directory(file_prefix);
  }

  obj_type = clobber_prefix + obj_type;

  os << nl << "cdef set_" << d.id << "(" << mainclass << " self, " << obj_type << " other)";
}

void gen_union_access_methods_pyx(std::ostream& os, const std::string& mainclass, const rpc_decl& d, const std::string& clobber_prefix, const std::string& file_prefix) {
  auto obj_type = py_type(d, file_prefix);

  if (is_native_type(d)) { //TODO was d.type
    obj_type = py_typename_native(d.type);
  }

  obj_type = clobber_prefix + obj_type;

  os << nl << "@property"
     << nl << "def " << d.id << "(self):";
     ++nl; 
     gen_null_check_pyx(os);
     //os << nl << "cdef " << c_typename_prefix << obj_type << " obj = deref(self.ptr)."<< d.id << "()";
     //os << nl << "return " << obj_type << ".from_ptr(addr(obj))";
     os << nl << "return " << obj_type << ".from_ptr(addr(deref(self.ptr)." << d.id << "()), ownership = False)";
     --nl;
  os << nl;

  os << nl << "cdef set_" << d.id << "(" << mainclass << " self, " << obj_type << " other):";
  ++nl;
    gen_null_check_pyx(os);
    gen_null_check_pyx(os, "other");

    os << nl << "deref(self.ptr)._pxdi_set_" << d.id << "(other.get_xdr())";
   //   os << nl << "deref(addr(deref(self.ptr)." << d.id << "())) = other.get_xdr()"; // why is cython like this
  --nl;

  os << nl << "@" << d.id << ".setter";
  os << nl << "def " << d.id << "(self, value):";
  ++nl;
    os << nl << "self.set_" << d.id << "(" << obj_type << "(value))";
  --nl;

}

void gen_union_access_methods_pxd(std::ostream& os, const std::string& mainclass, const rpc_decl& d, const std::string& clobber_prefix,  const std::string& file_prefix) {

  auto obj_type = py_type(d, file_prefix);

  if (is_native_type(d)) { //TODO was d.type
    obj_type = py_typename_native(d.type);
  }

  obj_type = clobber_prefix + obj_type;

  os << nl << "cdef set_" << d.id << "(" << mainclass << " self, " << obj_type << " other)";
}


void gen_constant_pyx(std::ostream& os, const rpc_const& c, const std::string& file_prefix) {
  os << nl << c.id << " = " << c.val;
  add_to_module_export(c.id);
  constants_map[c.id] = c.val;
}

void gen_constant_pxdi(std::ostream& os, const rpc_const& c, const std::string& file_prefix) {
  constants_map[c.id] = c.val;
}

void gen_constant_pxd(std::ostream& os, const rpc_const& c, const std::string& file_prefix) {
  constants_map[c.id] = c.val;
}

void gen_struct_pyx(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass = false);
void gen_struct_pxd(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass = false);
void gen_struct_pxdi(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass = false);

void gen_union_pyx(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass = false);
void gen_union_pxd(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass = false);
void gen_union_pxdi(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass = false);

/*
void gen_subtype_declaration_pxdi(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
   switch(decl.ts_which) {
    case rpc_decl::TS_ENUM:
      gen_enum_pxdi(os, *decl.ts_enum, file_prefix);
      break;
    case rpc_decl::TS_STRUCT:
      gen_struct_pxdi(os, *decl.ts_struct, file_prefix);
      break;
    case rpc_decl::TS_UNION:
      gen_union_pxdi(os, *decl.ts_union, file_prefix);
      break;
    default:
      assert(!"invalid subtype decl type");
  }
} */

void gen_flattened_decls_pxd(std::ostream& os, const rpc_struct& s, const std::string& file_prefix);
void gen_flattened_decls_pxd(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix);
void gen_flattened_decls_pxd(std::ostream& os, const rpc_union& u, const std::string& file_prefix);

void gen_flattened_decls_pxd(std::ostream& os, const rpc_struct& s, const std::string& file_prefix) {
  for (const auto& decl : s.decls) {
    if (needs_flattened_declaration(decl)) {
      gen_flattened_decl_pxd(os, decl, file_prefix);
    }
    if (is_native_type(decl)) {//TODO was d.type
      gen_native_type_wrapper_pxd(os, decl.type, file_prefix);
    }
    gen_flattened_decls_pxd(os, decl, file_prefix);
  }
}

void gen_flattened_decls_pxd(std::ostream& os, const rpc_union& u, const std::string& file_prefix) {
  for (const auto &ufield : u.fields) {
    if (needs_flattened_declaration(ufield.decl)) {
      gen_flattened_decl_pxd(os, ufield.decl, file_prefix);
    }
    if (is_native_type(ufield.decl)) { //TODO was d.type
      gen_native_type_wrapper_pxd(os, ufield.decl.type, file_prefix);
    }
    gen_flattened_decls_pxd(os, ufield.decl, file_prefix);
  }
}

void gen_flattened_decls_pxd(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
  if (needs_flattened_declaration(decl)) {
    gen_flattened_decl_pxd(os, decl, file_prefix);
  }
  if (is_native_type(decl)) { //TODO was d.type
    gen_native_type_wrapper_pxd(os, decl.type, file_prefix);
  }

  switch(decl.ts_which) {
    case rpc_decl::TS_STRUCT:
      gen_flattened_decls_pxd(os, *decl.ts_struct, file_prefix);
      break;
    case rpc_decl::TS_UNION:
      gen_flattened_decls_pxd(os, *decl.ts_union, file_prefix);
      break;
    default:
      //nothing to do
      break;
  }
}

void gen_flattened_decls_pxdi(std::ostream& os, const rpc_struct& s, const std::string& file_prefix);
void gen_flattened_decls_pxdi(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix);
void gen_flattened_decls_pxdi(std::ostream& os, const rpc_union& u, const std::string& file_prefix);

void gen_flattened_decls_pxdi(std::ostream& os, const rpc_struct& s, const std::string& file_prefix) {
  for (const auto& decl : s.decls) {
    if (needs_flattened_declaration(decl)) {
      gen_flattened_decl_pxdi(os, decl, file_prefix);
    }
    gen_flattened_decls_pxdi(os, decl, file_prefix);
  }
}

void gen_flattened_decls_pxdi(std::ostream& os, const rpc_union& u, const std::string& file_prefix) {
  for (const auto &ufield : u.fields) {
    if (needs_flattened_declaration(ufield.decl)) {
      gen_flattened_decl_pxdi(os, ufield.decl, file_prefix);
    }
    gen_flattened_decls_pxdi(os, ufield.decl, file_prefix);
  }
}

void gen_flattened_decls_pxdi(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
  if (needs_flattened_declaration(decl)) {
    gen_flattened_decl_pxdi(os, decl, file_prefix);
  }

  switch(decl.ts_which) {
    case rpc_decl::TS_STRUCT:
      gen_flattened_decls_pxdi(os, *decl.ts_struct, file_prefix);
      break;
    case rpc_decl::TS_UNION:
      gen_flattened_decls_pxdi(os, *decl.ts_union, file_prefix);
      break;
    default:
      //nothing to do
      break;
  }
}

void gen_flattened_decls_pyx(std::ostream& os, const rpc_struct& s, const std::string& file_prefix);
void gen_flattened_decls_pyx(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix);
void gen_flattened_decls_pyx(std::ostream& os, const rpc_union& u, const std::string& file_prefix);

void gen_flattened_decls_pyx(std::ostream& os, const rpc_struct& s, const std::string& file_prefix) {
  for (const auto& decl : s.decls) {
    if (needs_flattened_declaration(decl)) {
      gen_flattened_decl_pyx(os, decl, file_prefix);
    }
    if (is_native_type(decl)) { //TODO was d.type
      gen_native_type_wrapper_pyx(os, decl.type, file_prefix);
    }
    gen_flattened_decls_pyx(os, decl, file_prefix);
  }
}

void gen_flattened_decls_pyx(std::ostream& os, const rpc_union& u, const std::string& file_prefix) {
  for (const auto &ufield : u.fields) {
    if (needs_flattened_declaration(ufield.decl)) {
      gen_flattened_decl_pxd(os, ufield.decl, file_prefix);
    }
    if (is_native_type(ufield.decl)) { //TODO was d.type
      gen_native_type_wrapper_pyx(os, ufield.decl.type, file_prefix);
    }
    gen_flattened_decls_pyx(os, ufield.decl, file_prefix);
  }

  if (is_native_type(u.tagtype)) {
    gen_native_type_wrapper_pyx(os, u.tagtype, file_prefix);
  }
}
void gen_flattened_decls_pyx(std::ostream& os, const rpc_decl& decl, const std::string& file_prefix) {
  if (needs_flattened_declaration(decl)) {
    gen_flattened_decl_pyx(os, decl, file_prefix);
  }

  if (is_native_type(decl)) { //TODO was d.type
    gen_native_type_wrapper_pyx(os, decl.type, file_prefix);
  }

  switch(decl.ts_which) {
    case rpc_decl::TS_STRUCT:
      gen_flattened_decls_pyx(os, *decl.ts_struct, file_prefix);
      break;
    case rpc_decl::TS_UNION:
      gen_flattened_decls_pyx(os, *decl.ts_union, file_prefix);
      break;
    default:
      //nothing to do
      break;
  }
}



bool gen_subtype_pxdi(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  switch(d.ts_which) {
    case rpc_decl::TS_STRUCT:
      gen_struct_pxdi(os, *d.ts_struct, file_prefix, true);
      return true;
    case rpc_decl::TS_UNION:
      gen_union_pxdi(os, *d.ts_union, file_prefix, true);
      return true;
    default:
      //nothing to do
      break;
  }
  return false;
}

bool gen_subtype_pxd(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {

  auto filename = strip_directory(file_prefix);

  auto new_file_prefix = filename + "_" + nested_decl_names.back();
  switch(d.ts_which) {
    case rpc_decl::TS_STRUCT:
      os << nl << "from " << filename << "_includes cimport " << c_typename_prefix << py_nested_decl_name_for_subtype_import() << endl;;
      gen_struct_pxd(os, *d.ts_struct, new_file_prefix, true);
      return true;
    case rpc_decl::TS_UNION:
      os << nl << "from " << filename << "_includes cimport " << c_typename_prefix << py_nested_decl_name_for_subtype_import() << endl;;
      gen_union_pxd(os, *d.ts_union, new_file_prefix, true);
      return true;
    default:
      //nothing to do
      break;
  }
  return false;
}

bool gen_subtype_pyx(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {
  auto filename = strip_directory(file_prefix);
  auto new_file_prefix = filename + "_" + nested_decl_names.back();
  switch(d.ts_which) {
    case rpc_decl::TS_STRUCT:
      os << nl << "from " << filename << "_includes cimport " << c_typename_prefix << py_nested_decl_name_for_subtype_import() << endl;;
      gen_struct_pyx(os, *d.ts_struct, new_file_prefix, true);
      return true;
    case rpc_decl::TS_UNION:
      os << nl << "from " << filename << "_includes cimport " << c_typename_prefix << py_nested_decl_name_for_subtype_import() << endl;;
      gen_union_pyx(os, *d.ts_union, new_file_prefix, true);
      return true;
    default:
      //nothing to do
      break;
  }
  return false;
}


void gen_struct_pxdi(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass) {

  string c_typename_str = "";
  for (auto s : nested_decl_names) {
    c_typename_str += c_typename_prefix + s + ".";
  }

  util_method_classnames.push_back(c_typename_str + c_typename_prefix + s.id);

  if (!subclass) {
      gen_extern_cdef(os, file_prefix);
    ++nl;
        gen_helper_assignment(os, c_typename_prefix + s.id);

    os << nl << "cdef cppclass " << c_typename_prefix << s.id << " \"" << cppclass_type_string(s.id) << "\":";
    ++nl;
  } else {
    os << nl << "cppclass " << c_typename_prefix << s.id << " \"" << s.id << "\":";
    ++nl;
  }
  gen_ctors_pxdi(os, c_typename_prefix + s.id);


  nested_decl_names.push_back(s.id);

  for (const auto& decl : s.decls) {
    gen_subtype_pxdi(os, decl, file_prefix);
    auto prefix_use = c_typename_prefix;
    if (is_native_type(decl)) { //TODO was d.type
      prefix_use = "";
    }
    os << nl << prefix_use << py_type(decl, file_prefix)<< " " << decl.id;
  }

  nested_decl_names.pop_back();

   os << endl;
  --nl;
  if (!subclass) {
    --nl;
  }

 
}

void gen_struct_pxd(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass) {

  std::set<string> clobbered_localnames;

  nested_decl_names.push_back(s.id);

  for (const auto & d : s.decls) {
    auto status = gen_subtype_pxd(os, d, file_prefix);
    if (status) {
      clobbered_localnames.insert(d.id);
    }
  }

  auto clobber_prefix = py_nested_decl_name();

  nested_decl_names.pop_back();

  auto mainclass_name = py_nested_decl_name() + s.id;

  auto xdr_typename = c_typename_prefix + mainclass_name;
  if (subclass) {
    xdr_typename = c_typename_prefix + py_nested_decl_name_for_subtype_import() + "." + c_typename_prefix + s.id;
  }

  gen_pointer_class_pxd(os, mainclass_name, xdr_typename);

  for (const auto & d : s.decls) {
    auto cur_prefix = std::string("");
    if (clobbered_localnames.find(d.id) != clobbered_localnames.end()) {
      cur_prefix = clobber_prefix;
    }
    gen_var_access_methods_pxd(os, mainclass_name, d, cur_prefix, file_prefix);
  }

  end_gen_pointer_class(os);

}

void gen_struct_pyx(std::ostream& os, const rpc_struct& s, const std::string& file_prefix, bool subclass) {

  std::set<string> clobbered_localnames;

  nested_decl_names.push_back(s.id);

  for (const auto & d: s.decls) {
    auto status = gen_subtype_pyx(os, d, file_prefix);
    if (status) {
      clobbered_localnames.insert(d.id);
    }
  }

  auto clobber_prefix = py_nested_decl_name();

  nested_decl_names.pop_back();

  auto mainclass_name = py_nested_decl_name() + s.id;

  auto xdr_typename = c_typename_prefix + mainclass_name;
  if (subclass) {
    xdr_typename = c_typename_prefix + py_nested_decl_name_for_subtype_import() + "." + c_typename_prefix + s.id;
  }

  gen_pointer_class_pyx(os, s.id, xdr_typename);

  for (const auto & d : s.decls) {

    auto cur_prefix = std::string("");
    if (clobbered_localnames.find(d.id) != clobbered_localnames.end()) {
      cur_prefix = clobber_prefix;
    }
    gen_var_access_methods_pyx(os, mainclass_name, d, cur_prefix, file_prefix);
  }

  end_gen_pointer_class(os);

  add_to_module_export(mainclass_name);

}
/*
void gen_equals_struct_pyx(std::ostream& os, const rpc_struct& s, const std::string& classname, const std::string& xdr_typename = "") {
  
  auto clobber_prefix = py_nested_decl_name();

  auto qualified_name = xdr_typename;
  if (qualified_name == "") {
      qualified_name = c_typename_prefix + clobber_prefix + classname;
  }

  auto clobbered_classname = clobber_prefix + classname;*/

void gen_union_pxd(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass) {


  std::set<string> clobbered_localnames;

  nested_decl_names.push_back(u.id);

  for (const auto & ufield: u.fields) {
    auto status = gen_subtype_pxd(os, ufield.decl, file_prefix);
    if (status) {
      clobbered_localnames.insert(ufield.decl.id);
    }
  }

  auto clobber_prefix = py_nested_decl_name();

  nested_decl_names.pop_back();

  auto mainclass_name = py_nested_decl_name() + u.id;


  auto xdr_typename = c_typename_prefix + mainclass_name;
  if (subclass) {
    xdr_typename = c_typename_prefix + py_nested_decl_name_for_subtype_import() + "." + c_typename_prefix + u.id;
  }


  gen_pointer_class_pxd(os, u.id, xdr_typename);

  for (const auto& ufield : u.fields) {
    auto field_type = py_type(ufield.decl, file_prefix);
    if (ufield.decl.type == "void") {
      continue;
    }

    auto cur_prefix = std::string("");
    if (clobbered_localnames.find(ufield.decl.id) != clobbered_localnames.end()) {
      cur_prefix = clobber_prefix;
    }
    gen_union_access_methods_pxd(os, mainclass_name, ufield.decl, cur_prefix, file_prefix);
  }

  string tagtype = u.tagtype;

  if (is_native_type(tagtype)) {
    tagtype = tagtype + "_wrapper";
  }


  os << nl << "cdef set_" << u.tagid << "(" << mainclass_name << " self, " << tagtype << " value)";

  end_gen_pointer_class(os);
}

void gen_union_pyx(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass) {

  std::set<string> clobbered_localnames;

  nested_decl_names.push_back(u.id);

  for (const auto & ufield: u.fields) {
    auto status = gen_subtype_pyx(os, ufield.decl, file_prefix);
    if (status) {
      clobbered_localnames.insert(ufield.decl.id);
    }
  }

  auto clobber_prefix = py_nested_decl_name();

  nested_decl_names.pop_back();

  auto mainclass_name = py_nested_decl_name() + u.id;

  auto xdr_typename = c_typename_prefix + mainclass_name;
  if (subclass) {
    xdr_typename = c_typename_prefix + py_nested_decl_name_for_subtype_import() + "." + c_typename_prefix + u.id;
  }

  gen_pointer_class_pyx(os, u.id, xdr_typename);

  for (const auto& ufield : u.fields) {
    auto field_type = py_type(ufield.decl, file_prefix);
    if (ufield.decl.type == "void") {
      continue;
    }

    auto cur_prefix = std::string("");
    if (clobbered_localnames.find(ufield.decl.id) != clobbered_localnames.end()) {
      cur_prefix = clobber_prefix;
    }
    gen_union_access_methods_pyx(os, mainclass_name, ufield.decl, cur_prefix, file_prefix);
  }

  string tagtype = u.tagtype;

  if (is_native_type(tagtype)) {
    tagtype = tagtype + "_wrapper";
  }

  os << nl << "@property"
     << nl << "def " << u.tagid << "(self):";
     ++nl;
     gen_null_check_pyx(os);
     os << nl << "return " << tagtype << "(deref(self.ptr)." << u.tagid << "())";
     --nl;

  os << nl << "cdef set_" << u.tagid << "(" << mainclass_name << " self, " << tagtype << " value):";
    ++nl;
    gen_null_check_pyx(os);
    gen_null_check_pyx(os, "value");
    os << nl << "deref(self.ptr)." << u.tagid << "(value.get_xdr())";
    --nl;

  os << nl << "@"<< u.tagid << ".setter"
    << nl << "def " << u.tagid << "(self, value):";
    ++nl;
    os << nl << "self.set_" << u.tagid << "(" << tagtype << "(value))";
    --nl;
    /*
    @property
  def type(self):
    self.null_check()
    return OperationType(deref(self.ptr).type())

  cdef set_type(Operation__body_t self, OperationType other):
    self.null_check()
    other.null_check()
    deref(self.ptr).type(other.get_xdr())

  @type.setter
  def type(self, value):
    self.null_check()
    self.set_type(OperationType(value))

  */

  end_gen_pointer_class(os);

  add_to_module_export(mainclass_name);

}


void gen_union_pxdi(std::ostream& os, const rpc_union& u, const std::string& file_prefix, bool subclass) {

  gen_helper_assignment(os, c_typename_prefix + u.id);


  string c_typename_str = "";
  for (auto s : nested_decl_names) {
    c_typename_str += c_typename_prefix + s + ".";
  }

  util_method_classnames.push_back(c_typename_str + c_typename_prefix + u.id);


  if (!subclass) {
    gen_extern_cdef(os, file_prefix);
    ++nl;
    os << nl << "cdef cppclass " << c_typename_prefix << u.id << " \"" << cppclass_type_string(u.id) << "\":";
    ++nl;
  } else {
    os << nl << "cppclass " << c_typename_prefix << u.id << " \"" << u.id << "\":";
    ++nl;
  }


  gen_ctors_pxdi(os, c_typename_prefix + u.id);


  nested_decl_names.push_back(u.id);

  for (const auto &ufield : u.fields) {
    auto field_type = py_type(ufield.decl, file_prefix);
    if (ufield.decl.type == "void") {
      continue;
    }
    gen_subtype_pxdi(os, ufield.decl, file_prefix);
    os << nl << c_typename_prefix << field_type << "& " << ufield.decl.id << "() except +";
    os << nl << "void _pxdi_set_" << ufield.decl.id << "(const " << c_typename_prefix << field_type << "&) except +";
  }

  nested_decl_names.pop_back();

  auto tagtype = py_typename_native(u.tagtype);
  if (!is_native_type(u.tagtype)) {
    tagtype = c_typename_prefix + tagtype;
  }

  os << nl << tagtype << " " << u.tagid << "()"
     << nl << c_typename_prefix << u.id << "& " << u.tagid << "(" << tagtype << ")";

  --nl;
  if (!subclass) {
    --nl;
  }

}


void gen_typedef_pxdi(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {

  auto py_name = py_type(d, file_prefix);
  auto xdr_name = xdr_type(d);

  auto underlying_t_prefix = c_typename_prefix;
  if (is_native_type(d.type)) { 
    underlying_t_prefix = "";
  }
  
  os << nl << "ctypedef " << underlying_t_prefix << py_name << " " << c_typename_prefix << d.id << endl;
  //util_method_classnames.push_back(c_typename_prefix + d.id);
}

void gen_typedef_pxd(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {

  auto py_name = py_type(d, file_prefix);
  auto xdr_name = xdr_type(d);

  if (is_native_type(d.type)) {
    gen_native_type_wrapper_pxd(os, d.type, file_prefix);
    py_name += "_wrapper_" + strip_directory(file_prefix);
  }

  os << nl << "cdef class " << d.id << "(" << py_name << "):";
  ++nl;
    gen_base_extensionclass_from_ptr_method_pxd(os, d.id, py_name, file_prefix);
  --nl;

  os << endl;
}

void gen_typedef_pyx(std::ostream& os, const rpc_decl& d, const std::string& file_prefix) {

  auto py_name = py_type(d, file_prefix);
  auto xdr_name = xdr_type(d);
  
  //if (needs_flattened_declaration(d)) {
  //  gen_flattened_decl_pyx(os, d, file_prefix);
  //}
  if (is_native_type(d.type)) {
    gen_native_type_wrapper_pyx(os, d.type, file_prefix);
    py_name += "_wrapper_" + strip_directory(file_prefix);
  }
  os << nl << "cdef class " << d.id << "(" << py_name << "):";
  ++nl;
    gen_base_extensionclass_from_ptr_method_pyx(os, d.id, py_name, file_prefix);
  --nl;

  os << endl;

  add_to_module_export(d.id);
}

std::string get_srpc_client_type_string(const rpc_vers& version) {
  return std::string("::xdr::srpc_client<") + cppclass_type_string(version.id) + ">";
}

std::string get_srpc_client_flattened_pyname(const rpc_vers& version) {
  return std::string("xdr_srpc_client_") + flatten_namespaces(cppclass_type_string(version.id));
}

std::string get_srpc_obj_type_string(const rpc_vers& version) {
  return cppclass_type_string(version.id);
}

std::string get_srpc_obj_flattened_pyname(const rpc_vers& version) {
  return flatten_namespaces(cppclass_type_string(version.id));
}

void gen_srpc_client_method_pxdi(std::ostream& os, const rpc_proc& proc) {

  std::string out_type = proc.res;
  if (proc.res != "void") {
    out_type = std::string("unique_ptr[") + c_typename_prefix + proc.res + "]";
  }
  os << nl << out_type << " " << proc.id << "(";

  bool first = true;
  for (auto& arg : proc.arg) {
    if (!first) {
      os << ", ";
    }
    first = false;
    os << c_typename_prefix << arg;
  }
  os << ")";
}

void gen_srpc_client_method_pxd(std::ostream& os, const rpc_proc& proc) {
  std::string out_type = proc.res;
  if (proc.res != "void") {
    out_type = std::string("unique_ptr[") + c_typename_prefix + proc.res + "] "; // extra space at end
  } else {
    out_type = "";
  }

  os << nl << "cdef " << out_type << "_" << proc.id << "(self";

  for (size_t i = 0; i < proc.arg.size(); i++) {
    os << ", "  << proc.arg[i] << " arg" << i;
  }
  os << ")";
}

void gen_srpc_client_method_pyx(std::ostream& os, const rpc_proc& proc) {
  std::string out_type = proc.res;
  bool has_out = false;
  if (proc.res != "void") {
    out_type = std::string("unique_ptr[") + c_typename_prefix + proc.res + "] "; // extra space at end
    has_out = true;
  } else {
    out_type = "";
  }

  os << nl << "cdef " << out_type << "_" << proc.id << "(self";

  for (size_t i = 0; i < proc.arg.size(); i++) {
    os << ", "  << proc.arg[i] << " arg" << i;
  }
  os << "):";
  ++nl;
    gen_null_check_pyx(os);
    if (has_out) {
      os << nl << "return deref(self.ptr)." << proc.id << "(";
    } else {
      os << nl << "deref(self.ptr)." << proc.id << "(";
    }
    for (size_t i = 0; i < proc.arg.size(); i++) {
      if (i > 0) {
        os << ", ";
      }
      os << "arg" << i << ".get_xdr()";
    }
    os << ")";
  --nl;

  os << nl << "def " << proc.id << "(self";
  for (size_t i = 0; i < proc.arg.size(); i++) {
    os << ", arg" << i;
  }
  os << "):";
  ++nl;
    if (has_out) {

      os << nl << "return " << proc.res << ".from_ptr(";
    } else {
      os << nl;
    }
    os << "self._" << proc.id << "(";
    //function args
    for (size_t i = 0; i < proc.arg.size(); i++) {
      if (i > 0) {
        os << ", ";
      }
      os << "arg" << i;
    }
    os << ")";
    if (has_out) {
      os << ".release(), True)";
    }
  --nl;

}

void gen_srpc_client_pyx(std::ostream& os, const rpc_vers& version) {
  auto xdr_typename = c_typename_prefix + get_srpc_client_flattened_pyname(version);

  gen_pointer_class_pyx(os, version.id, xdr_typename, false, "int socketfd", "socketfd");

  for (auto& proc : version.procs) {
    gen_srpc_client_method_pyx(os, proc);
  }
  end_gen_pointer_class(os);

  add_to_module_export(version.id);
}


void gen_srpc_client_pxd(std::ostream& os, const rpc_vers& version) {
  auto xdr_typename = c_typename_prefix + get_srpc_client_flattened_pyname(version);

  gen_pointer_class_pxd(os, version.id, xdr_typename, false, "int");

  for (auto& proc : version.procs) {
    gen_srpc_client_method_pxd(os, proc);
  }
  end_gen_pointer_class(os);
}

void add_extern_empty_cc_import_pxdi(std::ostream& os, const std::string import_filename) {
  os << nl << "cdef extern from \"" << import_filename << "\":";
  os << nl << "  pass";
}

void gen_srpc_client_pxdi(std::ostream& os, const rpc_vers& version) {
  auto xdr_name = get_srpc_client_type_string(version);
  auto py_name = get_srpc_client_flattened_pyname(version);

  gen_extern_cdef(os, file_prefix);
  ++nl;
    os << nl << "cdef cppclass " << c_typename_prefix << get_srpc_obj_flattened_pyname(version) << " \"" << get_srpc_obj_type_string(version) << "\":";
    os << nl << "  pass";
  --nl;
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/srpc.cc>");
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/printer.cc>");
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/rpc_msg.cc>");
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/server.cc>");
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/msgsock.cc>");
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/socket.cc>");
  // this won't work right on windows
  add_extern_empty_cc_import_pxdi(os, "<xdrpp/socket_unix.cc>");

  add_extern_empty_cc_import_pxdi(os, "<xdrpp/pollset.cc>");

  os << nl << "cdef extern from \"<xdrpp/srpc.h>\" namespace \"::xdr\":";

  ++nl;
    os << nl << "cdef cppclass " << c_typename_prefix << py_name << " \"" << xdr_name << "\":";

    ++nl;
      os << nl << c_typename_prefix << py_name << "(int) except +";
      os << nl << c_typename_prefix << py_name << "() except +";
      os << nl << c_typename_prefix << py_name << "(const " << c_typename_prefix << py_name << "&) except +";

      for (auto& proc : version.procs) {
        gen_srpc_client_method_pxdi(os, proc);
      }
    --nl;
  --nl;
}

void gen_srpc_client_pyx(std::ostream& os, const rpc_program& program) {
  for (auto& vers : program.vers) {
    gen_srpc_client_pyx(os, vers);
  }
}

void gen_srpc_client_pxd(std::ostream& os, const rpc_program& program) {
  for (auto& vers : program.vers) {
    gen_srpc_client_pxd(os, vers);
  }
}

void gen_srpc_client_pxdi(std::ostream& os, const rpc_program& program) {
  for (auto& vers : program.vers) {
    gen_srpc_client_pxdi(os, vers);
  }
}




} /* namespace <noname> */

void gen_header_common(std::ostream& os) {
    os << "# distutils: language = c++" <<endl
     << "# cython: language_level = 3" <<endl
     << endl;
}

void gen_header_pxdi(std::ostream& os) {
  gen_header_common(os);

  os << "from libc.stdint cimport *" << endl
     << "from libcpp cimport bool" << endl
     << "from libcpp.memory cimport *" << endl;
}

void gen_header_pyx(std::ostream& os) {
  gen_header_common(os);

  os << "from libcpp cimport bool" << endl
     << "from libc.stdint cimport *" << endl
     << "from libcpp.string cimport *" << endl
     << "from libcpp.memory cimport *" << endl
     << "from cython.operator cimport dereference as deref" << endl
     << "from cython.operator cimport address as addr" << endl;
}

void gen_header_pxd(std::ostream& os) {
  gen_header_common(os);

  os << "from libcpp cimport bool" << endl
     << "from libc.stdint cimport *" << endl
     << "from libcpp.string cimport *" << endl
     << "from libcpp.memory cimport *" << endl;

}



void gen_total_flattened_decls_pxd(std::ostream& os, const std::string& file_prefix) {
  for (const auto &s : symlist) {
    switch(s.type) {
      case rpc_sym::TYPEDEF:
        gen_flattened_decls_pxd(os, *s.stypedef, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_flattened_decls_pxd(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_flattened_decls_pxd(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::CONST:
        gen_constant_pxd(os, *s.sconst, file_prefix);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
      case rpc_sym::LITERAL:
        os << *s.sliteral << endl;
        break;
      default:
        break; 
    }
  }
}

void gen_total_flattened_decls_pyx(std::ostream& os, const std::string& file_prefix) {
  for (const auto &s : symlist) {
    switch(s.type) {
      case rpc_sym::TYPEDEF:
        gen_flattened_decls_pyx(os, *s.stypedef, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_flattened_decls_pyx(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_flattened_decls_pyx(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::CONST:
        gen_constant_pyx(os, *s.sconst, file_prefix);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
      case rpc_sym::LITERAL:
        os << *s.sliteral << endl;
        break;
      default:
        break; 
    }
  }
}

void gen_total_flattened_decls_pxdi(std::ostream& os, const std::string& file_prefix) {
  for (const auto &s : symlist) {
    switch(s.type) {
      case rpc_sym::TYPEDEF:
        gen_flattened_decls_pxdi(os, *s.stypedef, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_flattened_decls_pxdi(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_flattened_decls_pxdi(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::CONST:
        gen_constant_pxdi(os, *s.sconst, file_prefix);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
      case rpc_sym::LITERAL:
        os << *s.sliteral << endl;
        break;
      default:
        break; 
    }
  }
}

string parse_included_filename(string literal) {
  if (literal.find("#include") != 0) {
    std::printf("can't parse this include statement");
    return "";
  }
  auto dot_idx = literal.find_last_of(".");
  auto slash_idx = literal.find_last_of("/");
  if (slash_idx == string::npos) {
    slash_idx = 0;
  }

  if (dot_idx == string::npos) {
    std::printf("no dot, can't parse");
    return "";
  }
  return literal.substr(slash_idx + 1, dot_idx - slash_idx - 1);
}

void gen_pxdi_util_methods(std::ostream& os) {
  os << nl << "cdef extern from \"<xdrpp/marshal.cc>\":"
     << nl << "  pass";
  os << nl << "cdef extern from \"<xdrpp/xdrpy_utils.h>\":";
  ++nl;
    for (auto& s : util_method_classnames) {
      os << nl << "cdef int load_xdr_from_file(" << s << "& output, const char* filename)";
      os << nl << "cdef int save_xdr_to_file(" << s << "& output, const char* filename)";
    }
  --nl;

  /*os << nl << "cdef extern from \"<xdrpp/srpc.cc\":"
     << nl << "  pass";
  os << nl << "cdef extern from \"<xdrpp/srpc.h\":";
  ++nl;
    for (auto &s : srpc_client_classes) {
      os << nl << "cdef cppclass "
    }*/
}

void gen_pxdi(std::ostream& os)
{
  gen_header_pxdi(os);
  gen_total_flattened_decls_pxdi(os, file_prefix);

  for (const auto &s : symlist) {
    switch(s.type) {
      case rpc_sym::TYPEDEF:
        gen_typedef_pxdi(os, *s.stypedef, file_prefix);
        break;
      case rpc_sym::ENUM:
        gen_enum_pxdi(os, *s.senum, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_struct_pxdi(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_union_pxdi(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::PROGRAM:
        gen_srpc_client_pxdi(os, *s.sprogram);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
      default:
        break; 
    }
  }

  gen_pxdi_util_methods(os);
}


void gen_pxd(std::ostream& os)
{
	gen_header_pxd(os);
  
  gen_native_type_wrapper_pxd(os, "uint8_t", file_prefix);
  gen_native_type_wrapper_pxd(os, "char", file_prefix);
  gen_native_type_wrapper_pxd(os, "float", file_prefix);

  //os << nl << "from " << file_prefix << "_includes cimport *" << endl;

  //os << nl << "cimport " << file_prefix << "_includes as " << import_qualifier;

  gen_total_flattened_decls_pxd(os, file_prefix);

	for (const auto &s : symlist) {
		switch(s.type) {
			case rpc_sym::TYPEDEF:
        gen_typedef_pxd(os, *s.stypedef, file_prefix);
				break;
      case rpc_sym::ENUM:
        gen_enum_pxd(os, *s.senum, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_struct_pxd(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_union_pxd(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::PROGRAM:
        gen_srpc_client_pxd(os, *s.sprogram);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
			default:
				break; 
		}
	}
}



void gen_pyx(std::ostream& os) {
  gen_header_pyx(os);
  os << "# sketchy hack" << endl;

  //os << nl << "cimport " << file_prefix << "_includes as " << file_prefix << endl;

  gen_native_type_wrapper_pyx(os, "uint8_t", file_prefix);
  gen_native_type_wrapper_pyx(os, "char", file_prefix);
  gen_native_type_wrapper_pyx(os, "float", file_prefix);

  gen_total_flattened_decls_pyx(os, file_prefix);


  for (const auto &s : symlist) {
    switch(s.type) {
      case rpc_sym::TYPEDEF:
        gen_typedef_pyx(os, *s.stypedef, file_prefix);
        break;
      case rpc_sym::ENUM:
        gen_enum_pyx(os, *s.senum, file_prefix);
        break;
      case rpc_sym::STRUCT:
        gen_struct_pyx(os, *s.sstruct, file_prefix);
        break;
      case rpc_sym::UNION:
        gen_union_pyx(os, *s.sunion, file_prefix);
        break;
      case rpc_sym::CONST:
        gen_constant_pyx(os, *s.sconst, file_prefix);
        break;
      case rpc_sym::PROGRAM:
        gen_srpc_client_pyx(os, *s.sprogram);
        break;
      case rpc_sym::NAMESPACE:
        namespaces.push_back(*s.sliteral);
        break;
      case rpc_sym::CLOSEBRACE:
        namespaces.pop_back();
        break;
      default:
        break;
    }
  }

  if (export_modules != "") {
    os << "__all__ = " << export_modules << "]";
  }
}
