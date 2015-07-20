#ifndef SHOWERRECOALGMODULAR_CXX
#define SHOWERRECOALGMODULAR_CXX

#include "ShowerRecoAlgModular.h"

namespace showerreco {

    Shower_t ShowerRecoAlgModular::RecoOneShower(const ShowerClusterSet_t& clusters){
      // Run over the shower reco modules:
      Shower_t result;

      for (auto & module : _modules){
        module -> do_reconstruction(clusters, result);
      }

      return result;
    }
    
    void ShowerRecoAlgModular::AddShowerRecoModule( ShowerRecoModuleBase * module){
      _modules.push_back(module);
    }

    
    void ShowerRecoAlgModular::ReplaceShowerRecoModule( ShowerRecoModuleBase * module, unsigned int index){
      if (_modules.size() > index){
        _modules.at(index) = module;
      }
      else{
        std::cerr << "WARNING: Request to remove non existing module at index " << index << std::endl;
      }
    }

    
    void ShowerRecoAlgModular::ReplaceShowerRecoModule( ShowerRecoModuleBase * module, std::string name){

      for (unsigned int index = 0; index < _modules.size(); index ++){
        if (_modules.at(index)->name() == name){
          _modules.at(index) = module;
          return;
        }
      }
      std::cerr << "WARNING: Request to remove non existing module \"" << name << "\"" << std::endl;

    }

} // showerreco

#endif
