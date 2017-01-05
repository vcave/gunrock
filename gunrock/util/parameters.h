// ----------------------------------------------------------------
// Gunrock -- Fast and Efficient GPU Graph Library
// ----------------------------------------------------------------
// This source code is distributed under the terms of LICENSE.TXT
// in the root directory of this source distribution.
// ----------------------------------------------------------------

/**
 * @file
 * parameters.h
 *
 * @brief Parameter class to hold running parameters
 */

#pragma once

#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <getopt.h>
#include <typeinfo>
#include <typeindex>
#include <gunrock/util/error_utils.cuh>
#include <gunrock/util/types.cuh>

namespace gunrock {
namespace util {

#define PARAMETER_DEBUG true

using Parameter_Flag = unsigned int;

enum {
    NO_ARGUMENT        = 0x01,
    REQUIRED_ARGUMENT  = 0x02,
    OPTIONAL_ARGUMENT  = 0x04,

    //ZERO_VALUE         = 0x10,
    SINGLE_VALUE       = 0x20,
    MULTI_VALUE        = 0x40,

    REQUIRED_PARAMETER = 0x100,
    OPTIONAL_PARAMETER = 0x200,
};

class Parameter_Item
{
public:
    std::string     name;
    Parameter_Flag  flag;
    std::string     default_value;
    std::string     description;
    std::string     value;
    bool            use_default;
    const std::type_info* value_type_info;
    std::string     file_name;
    int             line_num;

    Parameter_Item()
    :   name            (""),
        flag            (OPTIONAL_ARGUMENT | SINGLE_VALUE | OPTIONAL_PARAMETER),
        default_value   (""),
        description     (""),
        value           (""),
        use_default     (true),
        value_type_info (NULL),
        file_name       (""),
        line_num        (0)
    {
    }

    Parameter_Item(const std::type_info* value_tinfo)
    :   name            (""),
        flag            (OPTIONAL_ARGUMENT | SINGLE_VALUE | OPTIONAL_PARAMETER),
        default_value   (""),
        description     (""),
        value           (""),
        use_default     (true),
        value_type_info (value_tinfo),
        file_name       (""),
        line_num        (0)
    {
    }

    Parameter_Item(const Parameter_Item &item)
    :   name            (item.name),
        flag            (item.flag),
        default_value   (item.default_value),
        description     (item.description),
        value           (item.value),
        use_default     (item.use_default),
        value_type_info (item.value_type_info),
        file_name       (item.file_name),
        line_num        (item.line_num)
    {
    }
}; // Parameter_Item

class Parameters
{
private:
    std::map<std::string, Parameter_Item> p_map;
    std::string summary;

public:
    Parameters(
        std::string summary = "test <graph-type> [optional arguments]")
    :   summary(summary)
    {
        p_map.clear();
    }

    ~Parameters()
    {
        p_map.clear();
    }

    cudaError_t Use(
        std::string     name,
        Parameter_Flag  flag,
        std::string     default_value,
        std::string     description,
        const std::type_info* value_type_info,
        const char*     file_name,
        int             line_num)
    {
        // special case for no argument parameters
        if ((flag & NO_ARGUMENT) == NO_ARGUMENT)
        {
            if (std::type_index(*value_type_info) != std::type_index(typeid(bool)))
            {
                return GRError(cudaErrorInvalidValue,
                    "NO_ARGUMENT can only be applied to bool parameter, but "
                    + name + " is " + TypeName(value_type_info),
                    file_name, line_num);
            }

            if (default_value == "true")
            {
                std::cerr << "Warning: Bool parameter " << name
                    << "(" << file_name << ":" << line_num << ")"
                    << " with NO_ARGUMENT and true default value, has no effect"
                    << std::endl;
            }
        }

        Parameter_Item p_item(value_type_info);
        p_item.name           = name;
        p_item.flag           = flag;
        p_item.default_value  = default_value;
        p_item.description    = description;
        p_item.value          = default_value;
        p_item.use_default    = true;
        p_item.file_name      = std::string(file_name);
        p_item.line_num       = line_num;

        // test for duplication
        auto it = p_map.find(name);
        if (it != p_map.end()
            && (it -> second.file_name != std::string(file_name)
                || it -> second.line_num != line_num))
        {
            return GRError(cudaErrorInvalidSymbol,
                "Parameter " + name + " has been defined before, "
                + it -> second.file_name + ":" + std::to_string(it -> second.line_num), file_name, line_num);
        }

        p_map[name] = p_item;
        return cudaSuccess;
    }

    template <typename T>
    cudaError_t Use(
        std::string     name,
        Parameter_Flag  flag,
        T               default_value,
        std::string     description,
        const char*     file_name,
        int             line_num)
    {
        std::ostringstream ostr;
        ostr << default_value;
        return Use(name, flag,
            ostr.str(), description, &typeid(T),
            file_name, line_num);
    } // Use()

    cudaError_t Set(
        std::string name,
        std::string value)
    {
        // find the record
        auto it = p_map.find(name);
        if (it == p_map.end())
        {
            return GRError(cudaErrorInvalidValue,
                "Parameter " + name + " has not been defined", __FILE__, __LINE__);
        }

        if (PARAMETER_DEBUG)
            std::cout << "Parameter " << name << " <- "
                << value << std::endl;

        it -> second.value = value;
        it -> second.use_default = false;
        return cudaSuccess;
    }

    template <typename T>
    cudaError_t Set(
        std::string name,
        T           value)
    {
        std::ostringstream ostr;
        ostr << value;
        return Set(name, ostr.str());
    } // Set()

    cudaError_t Get(
        std::string name,
        std::string &value)
    {
        auto it = p_map.find(name);
        if (it == p_map.end())
        {
            return GRError(cudaErrorInvalidValue,
                "Parameter " + name + " has not been defined", __FILE__, __LINE__);
        }

        value = it -> second.value;
        return cudaSuccess;
    }

    template <typename T>
    cudaError_t Get(
        std::string name,
        T          &value)
    {
        std::string str_value;
        cudaError_t retval = Get(name, str_value);
        if (retval) return retval;

        std::istringstream istr(str_value);
        istr >> value;
        return cudaSuccess;
    }

    template <typename T>
    T Get(const char* name)
    {
        T val;
        Get(std::string(name), val);
        return val;
    }// Get()

    cudaError_t Check_Required()
    {
        for (auto it = p_map.begin(); it != p_map.end(); it++)
        {
            Parameter_Item &p_item = it -> second;
            if ((p_item.flag & REQUIRED_PARAMETER) != REQUIRED_PARAMETER)
                continue;
            if (p_item.value == "")
            {
                std::cerr << "Error : Required parameter " << p_item.name
                    << "(" << p_item.file_name << ":" << p_item.line_num << ")"
                    << " is not present." << std::endl;
            }
        }
        return cudaSuccess;
    }

    cudaError_t Parse_CommandLine(
        const int   argc,
        char* const argv[])
    {
        cudaError_t retval = cudaSuccess;
        typedef struct option Option;
        int num_options = p_map.size();
        Option *long_options = new Option[num_options + 1];
        std::string *names   = new std::string[num_options + 2];

        int i = 0;
        // load parameter list into long_options
        for (auto it = p_map.begin(); it != p_map.end(); it++)
        {
            long_options[i].name = it -> second.name.c_str();
            long_options[i].has_arg = ((it -> second.flag) & (0x07)) / 2;
            long_options[i].flag = NULL;
            long_options[i].val  = i+1;
            if (i+1 >= '?') long_options[i].val++;
            names[long_options[i].val] = it -> second.name;
            i++;
        }
        long_options[num_options].name = 0;
        long_options[num_options].has_arg = 0;
        long_options[num_options].flag = 0;
        long_options[num_options].val = 0;

        int option_index = 0;
        do {
            i = getopt_long_only (argc, argv, "", long_options, &option_index);
            switch (i)
            {
            case '?' :
                //std::cout << "Invalid parameter " << std::endl;
                break;

            case -1  :
                //end of known options
                break;

            default  :
                //std::cout << i << std::endl;
                if (i <= 0 || i > ((num_options + 1 >= '?') ? num_options + 2 : num_options + 1))
                {
                    std::cerr << "Invalid parameter" << std::endl;
                    break;
                }

                std::string argument(optarg == NULL ? "" : optarg);

                auto it = p_map.find(names[i]);
                Parameter_Item &p_item = it -> second;
                if ((std::type_index(*(p_item.value_type_info)) == std::type_index(typeid(bool))) && argument == "")
                {
                    argument = "true";
                }

                if ((p_item.flag & SINGLE_VALUE) == SINGLE_VALUE)
                {
                    if (argument.find(",") != std::string::npos)
                    {
                        std::cerr << "Error : Parameter " << p_item.name
                            << "(" << p_item.file_name << ":"
                            << p_item.line_num
                            << ") only takes single argument." << std::endl;
                        break;
                    }

                    if (!p_item.use_default)
                    {
                        std::cerr << "Warnning : Parameter " << p_item.name
                            << "(" << p_item.file_name << ":"
                            << p_item.line_num
                            << ") specified more than once, only latter value "
                            << argument << " is effective." << std::endl;
                    }
                }

                if ((p_item.flag & MULTI_VALUE) == MULTI_VALUE)
                {
                    if (!p_item.use_default)
                    {
                        std::cerr << "Warnning : Parameter " << p_item.name
                            << "(" << p_item.file_name << ":"
                            << p_item.line_num
                            << ") specified more than once, latter value "
                            << argument << " is appended to pervious ones." << std::endl;
                        argument = p_item.value + "," + argument;
                    }
                }

                if (!isValidString(argument, p_item.value_type_info))
                {
                    std::cerr << "Error : Parameter " << p_item.name
                        << "(" << p_item.file_name << ":"
                        << p_item.line_num
                        << ") only takes in " << TypeName(p_item.value_type_info)
                        << ", argument " << argument
                        << " is invalid." << std::endl;
                    break;
                }

                retval = Set(names[i], argument);
                break;
            }

            if (retval) break;
        } while (i!=-1);

        delete[] long_options; long_options = NULL;
        delete[] names; names = NULL;
        return retval;
    } // Phase_CommandLine()

    cudaError_t Print_Help()
    {
        std::cout << summary << std::endl;

        for (int t=0; t<2; t++)
        {
            bool first_parameter = true;
            Parameter_Flag selected_parameters
                = ((t == 0) ? REQUIRED_PARAMETER : OPTIONAL_PARAMETER);

            for (auto it = p_map.begin(); it != p_map.end(); it++)
            {
                // jump if not the selected ones
                if ((it -> second.flag & selected_parameters)
                    != selected_parameters)
                    continue;
                //std::cout << it -> second.flag << std::endl;
                if (first_parameter)
                {
                    if (selected_parameters == REQUIRED_PARAMETER)
                        std::cout << std::endl << "Required arguments:" << std::endl;
                    else
                        std::cout << std::endl << "Optional arguments:" << std::endl;
                    first_parameter = false;
                }

                std::cout << "--" << it -> second.name << " : "
                    << TypeName(it -> second.value_type_info)
                    << ", default = ";
                if (it -> second.default_value != "")
                {
                    if (std::type_index(*(it -> second.value_type_info))
                        == std::type_index(typeid(bool)))
                        std::cout << ((it -> second.default_value == "0") ? "false" : "true");
                    else
                        std::cout << it -> second.default_value;
                }
                std::cout << std::endl << "\t" << it -> second.description << std::endl;
            }
        }

        return cudaSuccess;
    } // Print_Help()

    std::map<std::string, std::string> List()
    {
        std::map<std::string, std::string> list;
        list.clear();

        for (auto it = p_map.begin(); it != p_map.end(); it ++)
        {
            list[it -> second.name] = it -> second.value;
        }
        return list;
    }

}; // class Parameters;

} // namespace util
} // nanespace gunrock

/*

1. Add new option in the long_options[] table
2. There are 4 options: {new option name, value option, NULL, class}
  2.1 Value option is an int: 0 for no value for this key, 1 for value must provided for this key,
      and 2 for value may or may not provided
  2.2 Class is an int: 1 for key without any values, 2 for key with only 1 value allowed.
  2.3 You may initialize new classes for special usage of the command line arguments
      If you want to add special usage, you must implement a new class in the switch table in the switch table
3. OptionValue is a class for special usage of the command line arguments, typical checking provided here are
  3.1 double_check() checks if the key has any duplicate values
    3.1.1 You may set up whether duplicate values allowed in disallowDuplicate()
  3.2 fileCheck checks if the key has values of file names that are NULL or cannot open

*/
/*#include <stdio.h>

#include <string.h>

#include <cstdlib>
#include <fstream>
#include <vector>


//double check, use only the last one



struct option long_options[] =
{
  {"market", mustHaveValue, NULL, marketClass},     //has to change her
  {"rgg",mustHaveValue, NULL, multiValueClass},
  {"instrument",mustHaveValue,NULL,oneValueClass},
  {"size_check",mustHaveValue,NULL,oneValueClass}, // has a not there
  {"debug_mode",mustHaveValue,NULL,oneValueClass},
  {"quick_mode",mustHaveValue,NULL,oneValueClass},
  {"quiet_mode",mustHaveValue,NULL,oneValueClass},
  {"idempotent",mustHaveValue,NULL,oneValueClass},// BFS
  {"mark_predecessors",mustHaveValue,NULL,oneValueClass},// BFS
  {"json",mustHaveValue,NULL,oneValueClass},
  {"jsonfile",mustHaveValue,NULL,multiValueClass},
  {"jsondir",mustHaveValue,NULL,multiValueClass},
  {"src",mustHaveValue,NULL,multiValueClass},  // (NOT SURE)
  {"grid-size",mustHaveValue,NULL,multiValueClass},
  {"iteration-num",mustHaveValue,NULL,multiValueClass},
  {"max-iter",mustHaveValue,NULL,multiValueClass},
  {"queue-sizing",mustHaveValue,NULL,multiValueClass},
  {"queue-sizing1",mustHaveValue,NULL,multiValueClass},
  {"partition_method",mustHaveValue,NULL,multiValueClass},
  {"partition-factor",mustHaveValue,NULL,multiValueClass},
  {"partition-seed",mustHaveValue,NULL,multiValueClass},
  {"traversal-mode",mustHaveValue,NULL,multiValueClass},
  {"ref_filename",mustHaveValue,NULL,multiValueClass},
  {"delta_factor",mustHaveValue,NULL,multiValueClass},
  {"delta",mustHaveValue,NULL,multiValueClass},
  {"error",mustHaveValue,NULL,multiValueClass},
  {"alpha",mustHaveValue,NULL,multiValueClass},
  {"beta",mustHaveValue,NULL,multiValueClass},
  {"top_nodes",mustHaveValue,NULL,multiValueClass},
  {"device_list",mustHaveValue,NULL,multiValueClass}, //NOT SURE IF WE STILL NEED THIS
  {"device",mustHaveValue,NULL,multiValueClass}, //IF ELSE
  //add your new options here above this line
  {0, 0, 0, 0},
};

class OptionValue{
  //this include file_type_setup,set_file_flag,content assign
private:
  vector<string> values;
  int num_iter = 0;
  bool valueDuplicateAllowed = true;

public:
  vector<string> keywords;
  OptionValue(){};

  void optionValueSetup(char* str) {contentParse(str);}
  void contentAssign(const string &name, std::multimap<string,string> &_m_map);
  bool double_check(multimap<string,string> &_m_map,string s_key,string s_value);
  void disallowDuplicate() {valueDuplicateAllowed = false;}
  void contentParse(char *str);
  void fileCheck();
  void clear();

  friend void keyword_check(char *str,char *_name,vector<string> _keywords);
};

class Commandline{
public:
  Commandline(){};
  void char_check(char *str);
  void printout(std::multimap<string,string> _m_map,string s);
  void keyword_check(char *str,char * _name,vector<string> _keywords);
  bool multiValuesCheck(std::multimap<string,string> &_m_map,string s_key,string s_value);

  multimap<string,string> commandlineArgument(int argc, char *argv[]);

};


//for multi value


multimap<string,string> Commandline::commandlineArgument(int argc, char *argv[]){
  int option_index;
  int c;
  char * pch;
  char *l_opt_arg;

  std::multimap<string,string> m_map;
  string s,tmpValue;
  OptionValue valueCheck;

  if (argc == 1){ //print help document
    printf("Help. \n");
    exit(1);
  }

  if(argv[1][0] != '-'){
    printf("Error. You need to put - for the options.\n");
    exit(1);
  }

  while((c = getopt_long_only (argc, argv, "", long_options, &option_index)) != -1){
    switch (c){
      case 0:
      printf("Invalid argument.\n");
      break;
      case zeroValueClass:
      s.assign(long_options[option_index].name);
      m_map.insert(make_pair(s,""));
      s.clear();
      break;
      case oneValueClass:
      s.assign(long_options[option_index].name);
      char_check(optarg);
      tmpValue.assign(optarg);
      if(tmpValue == "1") tmpValue = "true";
      if(tmpValue == "0") tmpValue = "false";
      if(!multiValuesCheck(m_map,s,tmpValue))
        m_map.insert(make_pair(s,tmpValue));
      s.clear();
      tmpValue.clear();
      break;
      case multiValueClass: //such as rgg
      s.assign(long_options[option_index].name);
      valueCheck.optionValueSetup(optarg);
      valueCheck.contentAssign(s,m_map);
      valueCheck.clear();
      s.clear();
      break;
			case marketClass: //market, argument
      s.assign(long_options[option_index].name);
      //valueCheck.keywords.push_back(".mtx");
      //valueCheck.keywords.push_back(".txt");
      valueCheck.optionValueSetup(optarg);
      valueCheck.fileCheck();
      valueCheck.disallowDuplicate();
      valueCheck.contentAssign(s,m_map);
      valueCheck.clear();
      break;
      //add new class here
      default:
      printf("Invalid argument.\n");
      break;
    }
}

  for(auto it=m_map.begin(), end = m_map.end(); it != end; it = m_map.upper_bound(it->first)){
    auto ret = m_map.equal_range(it->first);
    cout << it->first << "=> ";
    for(auto itValue = ret.first; itValue != ret.second; itValue++) cout << itValue->second << ' ';
    cout << endl;
  }
  return m_map;
}

//input map,string
bool Commandline::multiValuesCheck(std::multimap<string,string> &_m_map,string s_key,string s_value){
  multimap<string,string>::iterator it = _m_map.find(s_key);
  if(it != _m_map.end()) {
    if(s_value != it->second){
      printf("Warning: You have already set %s for the key --%s before.\nWarning: --%s is then set to %s.\n",it->second.c_str(),s_key.c_str(),s_key.c_str(),s_value.c_str());
      it->second = s_value;
    }
    return 1;
  }
  return 0;
}

void Commandline::char_check(char *str){
  int char_index=0;
  while (str[char_index]!='\0') {
    if(!std::isalnum(str[char_index])){
      printf("Value %s contains non-digit or non-letter characters.\n",str);
      exit(EXIT_FAILURE);
    }
    char_index++;
  }
}

void OptionValue::contentParse(char *str){ //check bracket and file/values
  int n = strlen(str);
  char *pch;

  pch = strtok (str," ,[]");
  while (pch != NULL){
    string s(pch);
    values.push_back(s);
    num_iter++;
    pch = strtok (NULL, " ,[]");
  }
}

void OptionValue::fileCheck(){
  for(string str:values){
    ifstream fp(str);
    if(str.empty() || !fp.is_open()){
      fprintf(stderr, "Input graph file %s does not exist.\n",str.c_str());
      exit (EXIT_FAILURE);
    }
    fp.close();
  }
}

bool OptionValue::double_check(multimap<string,string> &_m_map,string s_key,string s_value){
  // check if the key has duplicate values
  auto it = _m_map.find(s_key);
  if(it != _m_map.end()){
    auto ret = _m_map.equal_range(s_key);
    for(auto itValues = ret.first; itValues != ret.second; itValues++){
      if(s_value == itValues->second){
        if(valueDuplicateAllowed == false)
          printf("Duplicate value = %s for key = %s is not allowed. \n",itValues->second.c_str(), s_key.c_str());
          return true;
      }
    }
  }
  return false;
}

void OptionValue::contentAssign(const string &name, std::multimap<string,string> &_m_map){
  for(int i = 0; i < num_iter; i++){
    if(valueDuplicateAllowed == true || !double_check(_m_map,name,values[i]))
      _m_map.insert(make_pair(name,values[i]));
    else exit(1);
  }
}

void OptionValue::clear(){
  values.clear();
  num_iter = 0;
  keywords.clear();
  valueDuplicateAllowed = true;
}

//////////////////////////////// value,file checks checks*/

/*
void char_check_withcomma(char *str){
    int char_index=0;
    while (str[char_index]!='\0') {
      if(!std::isalnum(str[char_index])){
      printf("Value %s contains non-digit or non-letter characters.\n",str);
      exit(EXIT_FAILURE);
      }
      char_index++;
    }
}

void printout(std::multimap<string,string> _m_map,string s){
  multimap<string,string>::iterator beg,end;
  multimap<string,string>::iterator m;
  m = _m_map.find(s);
  beg = _m_map.lower_bound(s);
  end = _m_map.upper_bound(s);
  //int i = 0;
  for(m = beg;m != end;m++){
  cout<<m->first<<"--"<< m-> second <<endl;
  //cout << ++i << endl;
  }
}



void keyword_check(char *str,char * _name,vector<string> _keywords){
  if(!_keywords.empty()){
    //cout << _keywords[0] << endl;
    int i;
    string s(str);
    for(i = 0; i < _keywords.size(); i++){
      size_t found = s.find(_keywords[i]);
      if(found!=std::string::npos){
        break;
      }
    }
    if(i == _keywords.size()){
      printf("Your value %s does not match the keywords for key %s.\n",str,_name);
      exit(1);
    }
  }
}
*/

// Leave this at the end of the file
// Local Variables:
// mode:c++
// c-file-style: "NVIDIA"
// End:
