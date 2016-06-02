#ifndef WEBDB_DETPEDESTALRETRIEVALALG_CXX
#define WEBDB_DETPEDESTALRETRIEVALALG_CXX

#include "DetPedestalRetrievalAlg.h"
#include "WebError.h"
#include "larevt/CalibrationDBI/IOVData/IOVDataConstants.h"

// art/LArSoft libraries
#include "art/Framework/Services/Registry/ServiceHandle.h" 
#include "larcore/Geometry/Geometry.h"
#include "cetlib/exception.h"

//C/C++
#include <fstream>

namespace lariov {

  //constructors
  DetPedestalRetrievalAlg::DetPedestalRetrievalAlg(const std::string& foldername, 
      			      			   const std::string& url, 
			      			   const std::string& tag /*=""*/) : 
    DatabaseRetrievalAlg(foldername, url, tag),
    fDataSource(DataSource::Database) {
    
    fData.Clear();
    IOVTimeStamp tmp = IOVTimeStamp::MaxTimeStamp();
    tmp.SetStamp(tmp.Stamp()-1, tmp.SubStamp());
    fData.SetIoV(tmp, IOVTimeStamp::MaxTimeStamp());
  }
	
      
  DetPedestalRetrievalAlg::DetPedestalRetrievalAlg(fhicl::ParameterSet const& p) :
    DatabaseRetrievalAlg(p.get<fhicl::ParameterSet>("DatabaseRetrievalAlg")) {	
    
    this->Reconfigure(p);
  }
      
  void DetPedestalRetrievalAlg::Reconfigure(fhicl::ParameterSet const& p) {
    
    this->DatabaseRetrievalAlg::Reconfigure(p.get<fhicl::ParameterSet>("DatabaseRetrievalAlg"));
    fData.Clear();
    IOVTimeStamp tmp = IOVTimeStamp::MaxTimeStamp();
    tmp.SetStamp(tmp.Stamp()-1, tmp.SubStamp());
    fData.SetIoV(tmp, IOVTimeStamp::MaxTimeStamp());

    bool UseDB      = p.get<bool>("UseDB", false);
    bool UseFile   = p.get<bool>("UseFile", false);
    std::string fileName = p.get<std::string>("FileName", "");

    //priority:  (1) use db, (2) use table, (3) use defaults
    //If none are specified, use defaults
    if ( UseDB )      fDataSource = DataSource::Database;
    else if (UseFile) fDataSource = DataSource::File;
    else              fDataSource = DataSource::Default;

    if (fDataSource == DataSource::Default) {
      std::cout << "Using default pedestal values\n";
      float default_collmean     = p.get<float>("DefaultCollMean", 400.0);
      float default_collrms      = p.get<float>("DefaultCollRms", 0.3);
      float default_mean_err     = p.get<float>("DefaultMeanErr", 0.0);
      float default_rms_err      = p.get<float>("DefaultRmsErr", 0.0);
      float default_indmean      = p.get<float>("DefaultIndMean", 2048.0);
      float default_indrms       = p.get<float>("DefaultIndRms", 0.3);
      
      DetPedestal DefaultColl(0);
      DetPedestal DefaultInd(0);
      
      DefaultColl.SetPedMean(default_collmean);
      DefaultColl.SetPedMeanErr(default_mean_err);
      DefaultColl.SetPedRms(default_collrms);
      DefaultColl.SetPedRmsErr(default_rms_err);
      
      DefaultInd.SetPedMean(default_indmean);
      DefaultInd.SetPedMeanErr(default_mean_err);
      DefaultInd.SetPedRms(default_indrms);
      DefaultInd.SetPedRmsErr(default_rms_err);
      
      art::ServiceHandle<geo::Geometry> geo;
      geo::wire_id_iterator itW = geo->begin_wire_id();
      for ( ; itW != geo->end_wire_id(); ++itW) {
        DBChannelID_t ch = geo->PlaneWireToChannel(*itW);
      
        if (geo->SignalType(ch) == geo::kCollection) {
	  DefaultColl.SetChannel(ch);
	  fData.AddOrReplaceRow(DefaultColl);
	}
	else if (geo->SignalType(ch) == geo::kInduction) {
	  DefaultInd.SetChannel(ch);
	  fData.AddOrReplaceRow(DefaultInd);
	}
	else throw IOVDataError("Wire type is not collection or induction!");
      }
    }
    else if (fDataSource == DataSource::File) {
      std::cout << "Using pedestals from local file: "<<fileName<<"\n";
      std::ifstream file(fileName);
      if (!file) {
        throw cet::exception("DetPedestalRetrievalAlg")
	  << "File "<<fileName<<" is not found.";
      }
      
      std::string line;
      DetPedestal dp(0);
      while (std::getline(file, line)) {
        size_t current_comma = line.find(',');
        DBChannelID_t ch = (DBChannelID_t)std::stoi(line.substr(0, current_comma));	
	float ped = std::stof(line.substr(current_comma+1, line.find(',',current_comma+1)));
	
	current_comma = line.find(',',current_comma+1);
	float rms = std::stof(line.substr(current_comma+1, line.find(',',current_comma+1)));
	
	current_comma = line.find(',',current_comma+1);
	float ped_err = std::stof(line.substr(current_comma+1, line.find(',',current_comma+1)));
	
	current_comma = line.find(',',current_comma+1);
	float rms_err = std::stof(line.substr(current_comma+1, line.find(',',current_comma+1)));

	dp.SetChannel(ch);
	dp.SetPedMean(ped);
        dp.SetPedMeanErr(ped_err);
        dp.SetPedRms(rms);
        dp.SetPedRmsErr(rms_err);
	fData.AddOrReplaceRow(dp);
      }
    } // if source from file
    else { 
      std::cout << "Using pedestals from conditions database\n";
    }
  }


  bool DetPedestalRetrievalAlg::Update(DBTimeStamp_t ts) {
    
    if (fDataSource != DataSource::Database) return false;
      
    if (!this->UpdateFolder(ts)) return false;

    //DBFolder was updated, so now update the Snapshot
    fData.Clear();
    fData.SetIoV(this->Begin(), this->End());

    std::vector<DBChannelID_t> channels;
    fFolder->GetChannelList(channels);
    for (auto it = channels.begin(); it != channels.end(); ++it) {

      double mean, mean_err, rms, rms_err;
      fFolder->GetNamedChannelData(*it, "mean",     mean);
      fFolder->GetNamedChannelData(*it, "mean_err", mean_err);
      fFolder->GetNamedChannelData(*it, "rms",      rms);
      fFolder->GetNamedChannelData(*it, "rms_err",  rms_err);

      DetPedestal pd(*it);
      pd.SetPedMean( (float)mean );
      pd.SetPedMeanErr( (float)mean_err );
      pd.SetPedRms( (float)rms );
      pd.SetPedRmsErr( (float)rms_err );

      fData.AddOrReplaceRow(pd);
    }

    return true;

  }
  
  const DetPedestal& DetPedestalRetrievalAlg::Pedestal(DBChannelID_t ch) const {     
    return fData.GetRow(ch);
  }
      
  float DetPedestalRetrievalAlg::PedMean(DBChannelID_t ch) const {
    return this->Pedestal(ch).PedMean();
  }
  
  float DetPedestalRetrievalAlg::PedRms(DBChannelID_t ch) const {
    return this->Pedestal(ch).PedRms();
  }
  
  float DetPedestalRetrievalAlg::PedMeanErr(DBChannelID_t ch) const {
    return this->Pedestal(ch).PedMeanErr();
  }
  
  float DetPedestalRetrievalAlg::PedRmsErr(DBChannelID_t ch) const {
    return this->Pedestal(ch).PedRmsErr();
  }



}//end namespace lariov
	
#endif
        