//
// Prototype analysis module using tracks.  This module associates information from the
// Mu2e detector systems into a single coherent analysis TTree (trkana).  This module
// depends on the data products produced by reconstruction and (if requested) their MC
// counterparts.  The primary analysis object is the set of Downstream electron track fits.
// Upstream electron fit and downstream muon are also required for PID and quality selection.
// Calorimeter clusters and Track-cluster matching are used for PID. CRV coincidences are also
// included for rejecting cosmic backgrounds.
// Most of the calcluations are done by upstream modules and helper classes.
// Original author: Dave Brown (LBNL) 7/7/2016
// Updated November 2018 to run on KalSeeds only (A. Edmonds)
//

// Mu2e includes
#include "Offline/GeneralUtilities/inc/ParameterSetHelpers.hh"
#include "Offline/MCDataProducts/inc/ProtonBunchIntensity.hh"
#include "Offline/MCDataProducts/inc/EventWeight.hh"
#include "Offline/MCDataProducts/inc/KalSeedMC.hh"
#include "Offline/MCDataProducts/inc/CaloClusterMC.hh"
#include "Offline/MCDataProducts/inc/ProtonBunchTimeMC.hh"
#include "Offline/RecoDataProducts/inc/KalSeed.hh"
#include "Offline/RecoDataProducts/inc/KalSeedAssns.hh"
#include "Offline/RecoDataProducts/inc/CaloHit.hh"
#include "Offline/RecoDataProducts/inc/TrkCaloHitPID.hh"
#include "Offline/RecoDataProducts/inc/ProtonBunchTime.hh"
#include "Offline/TrkReco/inc/TrkUtilities.hh"
#include "Offline/CalorimeterGeom/inc/DiskCalorimeter.hh"
#include "Offline/GeometryService/inc/VirtualDetector.hh"
#include "Offline/RecoDataProducts/inc/CrvCoincidenceCluster.hh"
#include "Offline/MCDataProducts/inc/CrvCoincidenceClusterMC.hh"
#include "Offline/MCDataProducts/inc/CrvCoincidenceClusterMCAssns.hh"
#include "Offline/Mu2eUtilities/inc/fromStrings.hh"
#include "Offline/TrackerGeom/inc/Tracker.hh"
#include "Offline/GeometryService/inc/GeomHandle.hh"
#include "Offline/KinKalGeom/inc/SurfaceId.hh"
// Framework includes.
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Principal/Handle.h"
#include "art_root_io/TFileService.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "canvas/Persistency/Common/TriggerResults.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/OptionalAtom.h"
#include "fhiclcpp/types/Table.h"
#include "fhiclcpp/types/OptionalSequence.h"

// ROOT incldues
#include "Rtypes.h"
#include "TBits.h"
#include "TTree.h"
#include "TProfile.h"

// BaBar includes
#include "BTrk/BaBar/BaBar.hh"
#include "BTrk/KalmanTrack/KalRep.hh"
#include "BTrk/ProbTools/ChisqConsistency.hh"
#include "BTrk/BbrGeom/BbrVectorErr.hh"
#include "BTrk/TrkBase/TrkHelixUtils.hh"
#include "Offline/Mu2eUtilities/inc/TriggerResultsNavigator.hh"
// mu2e tracking
#include "Offline/BTrkData/inc/TrkStrawHit.hh"
// diagnostics
#include "TrkAna/inc/TrkComp.hh"
#include "TrkAna/inc/HitCount.hh"
#include "TrkAna/inc/TrkCount.hh"
#include "TrkAna/inc/EventInfo.hh"
#include "TrkAna/inc/TrkInfo.hh"
#include "TrkAna/inc/SimInfo.hh"
#include "TrkAna/inc/EventWeightInfo.hh"
#include "TrkAna/inc/TrkStrawHitInfo.hh"
#include "TrkAna/inc/TrkStrawHitInfoMC.hh"
#include "TrkAna/inc/TrkCaloHitInfo.hh"
#include "TrkAna/inc/CaloClusterInfoMC.hh"
#include "TrkAna/inc/TrkQualInfo.hh"
#include "TrkAna/inc/TrkPIDInfo.hh"
#include "TrkAna/inc/HelixInfo.hh"
#include "TrkAna/inc/InfoStructHelper.hh"
#include "TrkAna/inc/InfoMCStructHelper.hh"
#include "Offline/RecoDataProducts/inc/RecoQual.hh"
#include "TrkAna/inc/RecoQualInfo.hh"
#include "TrkAna/inc/BestCrvAssns.hh"
#include "TrkAna/inc/MCStepInfo.hh"
#include "Offline/CRVAnalysis/inc/CRVAnalysis.hh"

// C++ includes.
#include <iostream>
#include <string>
#include <cmath>

using namespace std;

namespace mu2e {
  // Need this for the BaBar headers.
  using CLHEP::Hep3Vector;
  typedef KalSeedCollection::const_iterator KSCIter;
  typedef size_t BranchIndex;

  class TrkAnaTreeMaker : public art::EDAnalyzer {

    public:

      struct BranchOptConfig {
        using Name=fhicl::Name;
        using Comment=fhicl::Comment;
        fhicl::Atom<bool> fillmc{Name("fillMC"), Comment("Switch to turn on filling of MC information for this set of tracks"), false};
        fhicl::Atom<bool> fillhits{Name("fillHits"), Comment("Switch to turn on filling of hit-level information for this set of tracks"), false};
        fhicl::OptionalAtom<std::string> trkqual{Name("trkqual"), Comment("TrkQualCollection input tag to be written out (use prefix if fcl parameter suffix (e.g. DeM) is defined)")};
        fhicl::Atom<bool> filltrkqual{Name("fillTrkQual"), Comment("Switch to turn on filling of the full TrkQualInfo for this set of tracks"), false};
        fhicl::OptionalAtom<std::string> trkpid{Name("trkpid"), Comment("TrkCaloHitPIDCollection input tag to be written out (use prefix if fcl parameter suffix (e.g. DeM) is defined)")};
        fhicl::Atom<bool> filltrkpid{Name("fillTrkPID"), Comment("Switch to turn on filling of the full TrkPIDInfo for this set of tracks"), false};
        fhicl::Atom<bool> required{Name("required"), Comment("True/false if you require this type of track in the event"), false};
        fhicl::Atom<int> genealogyDepth{Name("genealogyDepth"), Comment("The depth of the genealogy information you want to keep"), 1};
        fhicl::OptionalSequence<std::string> bestCrvModules{Name("bestCrvModules"), Comment("Sequence of module labels that create the BestCrvAssns you want written out (use prefix if fcl parameter suffix (e.g. DeM) is defined)")};
        fhicl::OptionalSequence<std::string> bestCrvInstances{Name("bestCrvInstances"), Comment("Sequence of instance names for modules that create multiple BestCrvAssns")};
        fhicl::OptionalSequence<std::string> bestCrvBranches{Name("bestCrvBranches"), Comment("Sequence of branch names that will be created to store the bestcrv information")};
        fhicl::Atom<bool> fillbestcrv{Name("fillBestCrv"), Comment("Switch to turn on filling of the bestcrv branch for this set of tracks"), false};
      };

      struct BranchConfig {
        using Name=fhicl::Name;
        using Comment=fhicl::Comment;

        fhicl::Atom<std::string> input{Name("input"), Comment("KalSeedCollection input tag (use prefix if fcl parameter suffix is defined)")};
        fhicl::Atom<std::string> branch{Name("branch"), Comment("Name of output branch")};
        fhicl::Atom<std::string> suffix{Name("suffix"), Comment("Fit suffix (e.g. DeM)"), ""};
        fhicl::Table<BranchOptConfig> options{Name("options"), Comment("Optional arguments for a branch")};
      };

      struct Config {
        using Name=fhicl::Name;
        using Comment=fhicl::Comment;

        // General control and config
        fhicl::Atom<bool> pempty{Name("ProcessEmptyEvents"),false};
        fhicl::Atom<int> diag{Name("diagLevel"),1};
        fhicl::Atom<int> debug{Name("debugLevel"),0};
        fhicl::Atom<int> splitlevel{Name("splitlevel"),99};
        fhicl::Atom<int> buffsize{Name("buffsize"),32000};
        // General event info
        fhicl::Atom<art::InputTag> rctag{Name("RecoCountTag"), Comment("RecoCount"), art::InputTag()};
        fhicl::Atom<art::InputTag> PBITag{Name("PBITag"), Comment("Tag for ProtonBunchIntensity object") ,art::InputTag()};
        fhicl::Atom<art::InputTag> PBTTag{Name("PBTTag"), Comment("Tag for ProtonBunchTime object") ,art::InputTag()};
        fhicl::Atom<bool> filltrig{Name("FillTriggerInfo"),false};
        fhicl::Atom<std::string> trigProcessName{Name("TriggerProcessName"), Comment("Process name for Trigger")};
        fhicl::Atom<std::string> trigpathsuffix{Name("TriggerPathSuffix"), "_trigger"}; // all trigger paths have this in the name
        // core tracking
        fhicl::Table<BranchConfig> candidate{Name("candidate"), Comment("Candidate physics track info")};
        fhicl::OptionalSequence< fhicl::Table<BranchConfig> > supplements{Name("supplements"), Comment("Supplemental physics track info (TrkAna will find closest in time to candidate)")};
        // Additional (optional) tracking information
        fhicl::Atom<bool> fillhits{Name("FillHitInfo"),Comment("Global switch to turn on/off hit-level info"), false};
        fhicl::Atom<std::string> fittype{Name("FitType"),Comment("Type of track Fit: LoopHelix, CentralHelix, KinematicLine, or Unknown"),"Unknown"};
        fhicl::Atom<bool> helices{Name("FillHelixInfo"),false};
        // CRV -- input tags
        fhicl::Atom<std::string> crvCoincidenceModuleLabel{Name("CrvCoincidenceModuleLabel"), Comment("CrvCoincidenceModuleLabel")};
        fhicl::Atom<std::string> crvRecoPulseLabel{Name("CrvRecoPulseLabel"), Comment("CrvRecoPulseLabel")};
        fhicl::Atom<std::string> crvStepLabel{Name("CrvStepLabel"), Comment("CrvStepLabel")};
        fhicl::Atom<std::string> crvWaveformsModuleLabel{ Name("CrvWaveformsModuleLabel"), Comment("CrvWaveformsModuleLabel")};
        fhicl::Atom<std::string> crvDigiModuleLabel{ Name("CrvDigiModuleLabel"), Comment("CrvDigiModuleLabel")};
        // CRV -- flags
        fhicl::Atom<bool> crv{Name("FillCRV"),Comment("Flag for turning on bestcrv(mc) branches"), false};
        fhicl::Atom<bool> crvhits{Name("FillCRVHits"), Comment("Flag for turning on crvinfo(mc), crvsummary(mc), and crvinfomcplane branches"), false};
        fhicl::Atom<bool> crvpulses{Name("FillCRVPulses"),Comment("Flag for turning on crvpulseinfo(mc), crvwaveforminfo branches"), false};
        // CRV -- other
        fhicl::Atom<double> crvPlaneY{Name("CrvPlaneY"),2751.485};  //y of center of the top layer of the CRV-T counters
        // MC truth
        fhicl::Atom<bool> fillmc{Name("FillMCInfo"),Comment("Global switch to turn on/off MC info"),true};
        fhicl::Table<InfoMCStructHelper::Config> infoMCStructHelper{Name("InfoMCStructHelper"), Comment("Configuration for the InfoMCStructHelper")};
        fhicl::Atom<art::InputTag> PBTMCTag{Name("PBTMCTag"), Comment("Tag for ProtonBunchTimeMC object") ,art::InputTag()};
        fhicl::Atom<std::string> simParticleLabel{Name("SimParticleLabel"), Comment("SimParticleLabel")};
        fhicl::Atom<std::string> mcTrajectoryLabel{Name("MCTrajectoryLabel"), Comment("MCTrajectoryLabel")};
        fhicl::Atom<art::InputTag> primaryParticleTag{Name("PrimaryParticleTag"), Comment("Tag for PrimaryParticle"), art::InputTag()};
        fhicl::Atom<art::InputTag> kalSeedMCTag{Name("KalSeedMCAssns"), Comment("Tag for KalSeedMCAssn"), art::InputTag()};
        fhicl::OptionalSequence<art::InputTag> extraMCStepTags{Name("ExtraMCStepCollectionTags"), Comment("Input tags for any other StepPointMCCollections you want written out")};
        // Calo MC
        fhicl::Atom<bool> fillCaloMC{ Name("FillCaloMC"),Comment("Fill CaloMC information"), true};
        fhicl::Atom<art::InputTag> caloClusterMCTag{Name("CaloClusterMCTag"), Comment("Tag for CaloClusterMCCollection") ,art::InputTag()};
        // CRV MC
        fhicl::Atom<std::string> crvCoincidenceMCModuleLabel{Name("CrvCoincidenceMCModuleLabel"), Comment("CrvCoincidenceMCModuleLabel")};
        fhicl::Atom<art::InputTag> crvMCAssnsTag{ Name("CrvCoincidenceClusterMCAssnsTag"), Comment("art::InputTag for CrvCoincidenceClusterMCAssns")};
        // Pre-processed analysis info; are these redundant with the branch config ?
        fhicl::Atom<bool> filltrkqual{Name("FillTrkQualInfo"),false};
        fhicl::Atom<bool> filltrkpid{Name("FillTrkPIDInfo"),false};
      };
      typedef art::EDAnalyzer::Table<Config> Parameters;

      explicit TrkAnaTreeMaker(const Parameters& conf);
      virtual ~TrkAnaTreeMaker() { }

      void beginJob();
      void beginSubRun(const art::SubRun & subrun ) override;
      void analyze(const art::Event& e);

    private:

      Config _conf;
      std::vector<BranchConfig> _allBranches; // candidates + supplements
      BranchIndex _candidateIndex; // location in above vector that contains the candidate

      // main TTree
      TTree* _trkana;
      // general event info branch
      EventInfo _einfo;
      EventInfoMC _einfomc;
      art::InputTag _PBITag, _PBTTag, _PBTMCTag;
      std::vector<art::InputTag> _extraMCStepTags;
      std::vector<art::Handle<StepPointMCCollection>> _extraMCStepCollections;
      std::vector<std::vector<MCStepInfos*>> _extraMCStepInfos;
      std::vector<std::vector<MCStepSummaryInfo*>> _extraMCStepSummaryInfos;
      // hit counting
      HitCount _hcnt;
      // track counting
      TrkCount _tcnt;
      // track branches (inputs)
      std::vector<art::Handle<KalSeedCollection> > _allKSCHs;
      // track branches (outputs)
      std::vector<TrkInfo> _allTIs;
      std::map<BranchIndex, std::vector<TrkFitInfo>> _allTFIs;
      std::map<BranchIndex, std::vector<LoopHelixInfo>> _allLHIs;
      std::map<BranchIndex, std::vector<CentralHelixInfo>> _allCHIs;
      std::map<BranchIndex, std::vector<KinematicLineInfo>> _allKLIs;

      std::vector<TrkCaloHitInfo> _allTCHIs;
      // quality branches (inputs)
      std::vector<std::vector<art::Handle<RecoQualCollection> > > _allRQCHs; // outer vector is for each candidate/supplement, inner vector is all RecoQuals
      std::vector<art::Handle<TrkQualCollection> > _allTQCHs; // we will only allow one TrkQual object per candidate/supplement to be fully written out
      std::vector<art::Handle<TrkCaloHitPIDCollection> > _allTCHPCHs; // we will only allow one TrkCaloHitPID object per candidate/supplement to be fully written out
      // quality branches (outputs)
      std::vector<RecoQualInfo> _allRQIs;
      std::vector<TrkQualInfo> _allTQIs;
      std::vector<TrkPIDInfo> _allTPIs;
      // trigger information
      unsigned _trigbits;
      std::map<size_t,unsigned> _tmap; // map between path and trigger ID.  ID should come from trigger itself FIXME!
      // MC truth (fcl parameters)
      bool _fillmc;
      // MC truth inputs
      art::Handle<PrimaryParticle> _pph;
      art::Handle<KalSeedMCAssns> _ksmcah;
      art::InputTag _primaryParticleTag;
      // MC truth branches (outputs)
      std::vector<TrkInfoMC> _allMCTIs;
      std::map<BranchIndex, std::vector<SimInfo>> _allMCSimTIs;
      std::map<BranchIndex, std::vector<MCStepInfo>> _allMCVDInfos;
      bool _fillcalomc;
      art::Handle<CaloClusterMCCollection> _ccmcch;
      std::vector<CaloClusterInfoMC> _allMCTCHIs;

      // hit level info branches
      std::vector<std::vector<TrkStrawHitInfo>> _allTSHIs;
      std::vector<std::vector<TrkStrawMatInfo>> _allTSMIs;
      std::vector<std::vector<TrkStrawHitInfoMC>> _allTSHIMCs;

      // event weights
      std::vector<art::Handle<EventWeight> > _wtHandles;
      EventWeightInfo _wtinfo;
      // CRV
      // CRV -- fhicl parameters
      bool _crv;
      bool _crvhits;
      bool _crvpulses;
      std::string _crvCoincidenceModuleLabel;
      std::string _crvCoincidenceMCModuleLabel;
      std::string _crvRecoPulseLabel;
      std::string _crvStepLabel;
      std::string _crvWaveformsModuleLabel;
      std::string _crvDigiModuleLabel;
      art::InputTag _crvMCAssnsTag;
      double _crvPlaneY;
      // CRV (inputs)
      std::map<BranchIndex, std::vector<art::Handle<BestCrvAssns>>> _allBestCrvAssns;
      art::Handle<CrvCoincidenceClusterMCCollection> _crvCoincidenceMCCollHandle;
      art::Handle<CrvCoincidenceClusterMCAssns> _crvMCAssnsHandle;
      // CRV (output)
      std::vector<CrvHitInfoReco> _crvinfo;
      std::map<BranchIndex, std::vector<CrvHitInfoReco>> _allBestCrvs; // there can be more than one of these per candidate/supplement
      std::vector<CrvHitInfoMC> _crvinfomc;
      std::map<BranchIndex, std::vector<CrvHitInfoMC>> _allBestCrvMCs;
      CrvSummaryReco _crvsummary;
      CrvSummaryMC   _crvsummarymc;
      std::vector<CrvPlaneInfoMC> _crvinfomcplane;
      std::vector<CrvPulseInfoReco> _crvpulseinfo;
      std::vector<CrvWaveformInfo> _crvwaveforminfo;
      std::vector<CrvHitInfoMC> _crvpulseinfomc;
      // helices
      HelixInfo _hinfo;
      // struct helpers
      InfoStructHelper _infoStructHelper;
      InfoMCStructHelper _infoMCStructHelper;
      // branch structure
      Int_t _buffsize, _splitlevel;
      enum FType{Unknown=0,LoopHelix,CentralHelix,KinematicLine};
      FType _ftype = Unknown;
      std::vector<std::string> fitNames = {"Unknown", "LoopHelix","CentralHelix","KinematicLine"};

      // helper functions
      void fillEventInfo(const art::Event& event);
      void fillTriggerBits(const art::Event& event,std::string const& process);
      void resetTrackBranches();
      size_t findSupplementTrack(KalSeedCollection const& kcol,KalSeed const& candidate, bool sameColl);
      void fillAllInfos(const art::Handle<KalSeedCollection>& ksch, BranchIndex i_branch, size_t i_kseed);

      template <typename T, typename TI, typename TIA>
        std::vector<art::Handle<T> > createSpecialBranch(const art::Event& event, const std::string& branchname,
            std::vector<art::Handle<T> >& handles, TI& infostruct, TIA& array, bool make_individual_branches = false, const std::string& selection = "");

  };

  TrkAnaTreeMaker::TrkAnaTreeMaker(const Parameters& conf):
    art::EDAnalyzer(conf),
    _conf(conf()),
    _PBITag(conf().PBITag()),
    _PBTTag(conf().PBTTag()),
    _PBTMCTag(conf().PBTMCTag()),
    _fillmc(conf().fillmc()),
    _fillcalomc(conf().fillCaloMC()),
    // CRV
    _crv(conf().crv()),
    _crvhits(conf().crvhits()),
    _crvpulses(conf().crvpulses()),
    _crvCoincidenceModuleLabel(conf().crvCoincidenceModuleLabel()),
    _crvCoincidenceMCModuleLabel(conf().crvCoincidenceMCModuleLabel()),
    _crvRecoPulseLabel(conf().crvRecoPulseLabel()),
    _crvStepLabel(conf().crvStepLabel()),
    _crvWaveformsModuleLabel(conf().crvWaveformsModuleLabel()),
    _crvDigiModuleLabel(conf().crvDigiModuleLabel()),
    _crvMCAssnsTag(conf().crvMCAssnsTag()),
    _crvPlaneY(conf().crvPlaneY()),
    _infoMCStructHelper(conf().infoMCStructHelper()),
    _buffsize(conf().buffsize()),
    _splitlevel(conf().splitlevel())
  {
    // decode fit type
    for(size_t ifit=0;ifit < fitNames.size();++ifit){
      auto const& fname = fitNames[ifit];
      if(_conf.fittype() == fname){
        _ftype= (FType)ifit;
        break;
      }
    }

    // collect both candidate and supplement branches into one place
    _allBranches.push_back(_conf.candidate());
    _candidateIndex = 0;
    std::vector<BranchConfig> supps;
    if (_conf.supplements(supps)) {
      for(const auto& i_supp : supps) {
        _allBranches.push_back(i_supp);
      }
    }

    // Create all the info structs
    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      auto i_branchConfig = _allBranches.at(i_branch);
      TrkInfo ti;
      _allTIs.push_back(ti);
      // fit sampling (KalIntersection) at a surface
      _allTFIs[i_branch] = std::vector<TrkFitInfo>();
      // fit-specific branches
      _allLHIs[i_branch] = std::vector<LoopHelixInfo>();
      _allCHIs[i_branch] = std::vector<CentralHelixInfo>();
      _allKLIs[i_branch] = std::vector<KinematicLineInfo>();

      // candidate mc truth info at VDs
      std::vector<MCStepInfo> allMCVDSteps;
      _allMCVDInfos[i_branch] = allMCVDSteps;

      TrkCaloHitInfo tchi;
      _allTCHIs.push_back(tchi);

      TrkInfoMC mcti;
      _allMCTIs.push_back(mcti);
      _allMCSimTIs[i_branch] = std::vector<SimInfo>();

      if(_fillcalomc){
        CaloClusterInfoMC mctchi;
        _allMCTCHIs.push_back(mctchi);
      }

      RecoQualInfo rqi;
      _allRQIs.push_back(rqi);
      TrkQualInfo tqi;
      _allTQIs.push_back(tqi);
      TrkPIDInfo tpi;
      _allTPIs.push_back(tpi);

      std::vector<TrkStrawHitInfo> tshi;
      _allTSHIs.push_back(tshi);
      std::vector<TrkStrawMatInfo> tsmi;
      _allTSMIs.push_back(tsmi);
      std::vector<TrkStrawHitInfoMC> tshimc;
      _allTSHIMCs.push_back(tshimc);

      if(_crv && i_branchConfig.options().fillbestcrv()) { // if we are filling in bestcrv information
        std::vector<CrvHitInfoReco> bestcrv;
        std::vector<CrvHitInfoMC> bestcrvmc;
        std::vector<std::string> bestCrvBranchNames; // need to know how many bestcrv branches there will be per candidate/supplement
        if (i_branchConfig.options().bestCrvBranches(bestCrvBranchNames)) {
          for (size_t i_bestCrvBranch = 0; i_bestCrvBranch < bestCrvBranchNames.size(); ++i_bestCrvBranch) {
            bestcrv.push_back(CrvHitInfoReco()); // add empty structs to the vector so that ROOT can be given a location to find it
            bestcrvmc.push_back(CrvHitInfoMC());
          }
          _allBestCrvs[i_branch] = bestcrv;
          _allBestCrvMCs[i_branch] = bestcrvmc;
        }
      }
    }
  }

  void TrkAnaTreeMaker::beginJob( ){
    art::ServiceHandle<art::TFileService> tfs;
    // create TTree
    _trkana=tfs->make<TTree>("trkana","track analysis");
    // add event info branch
    _trkana->Branch("evtinfo.",&_einfo,_buffsize,_splitlevel);
    _trkana->Branch("evtinfomc.",&_einfomc,_buffsize,_splitlevel);
    // hit counting branch
    _trkana->Branch("hcnt.",&_hcnt);
    // track counting branches
    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      BranchConfig i_branchConfig = _allBranches.at(i_branch);
      std::string leafname = i_branchConfig.branch();
      _trkana->Branch(("tcnt.n"+leafname).c_str(),&_tcnt._counts[i_branch]);
    }

    // pre-set all the storage used by branches so it doesn't move
    if(_conf.extraMCStepTags(_extraMCStepTags)){
      for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
        _extraMCStepInfos.push_back(std::vector<MCStepInfos*>(_extraMCStepTags.size()));
        _extraMCStepSummaryInfos.push_back(std::vector<MCStepSummaryInfo*>(_extraMCStepTags.size()));
        for(size_t iextra=0;iextra<_extraMCStepTags.size(); iextra++){
          _extraMCStepInfos.at(i_branch).at(iextra) = new MCStepInfos;
          _extraMCStepSummaryInfos.at(i_branch).at(iextra) = new MCStepSummaryInfo;
        }
      }
    }

    // create all candidate and supplement branches
    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      BranchConfig i_branchConfig = _allBranches.at(i_branch);
      std::string branch = i_branchConfig.branch();
      _trkana->Branch((branch+".").c_str(),&_allTIs.at(i_branch));
      _trkana->Branch((branch+"fit.").c_str(),&_allTFIs.at(i_branch),_buffsize,_splitlevel);
// add traj-specific branches
      if(_ftype == LoopHelix )_trkana->Branch((branch+"lh.").c_str(),&_allLHIs.at(i_branch),_buffsize,_splitlevel);
      if(_ftype == CentralHelix )_trkana->Branch((branch+"ch.").c_str(),&_allCHIs.at(i_branch),_buffsize,_splitlevel);
      if(_ftype == KinematicLine )_trkana->Branch((branch+"kl.").c_str(),&_allKLIs.at(i_branch),_buffsize,_splitlevel);
      // TrkCaloHit: currently only 1
      _trkana->Branch((branch+"tch.").c_str(),&_allTCHIs.at(i_branch));
      if (_conf.filltrkqual() && i_branchConfig.options().filltrkqual()) {
        int n_trkqual_vars = TrkQual::n_vars;
        for (int i_trkqual_var = 0; i_trkqual_var < n_trkqual_vars; ++i_trkqual_var) {
          TrkQual::MVA_varindex i_index =TrkQual::MVA_varindex(i_trkqual_var);
          std::string varname = TrkQual::varName(i_index);
          _trkana->Branch((branch+"trkqual."+varname).c_str(), &_allTQIs.at(i_branch).trkqualvars[i_index]);
        }
        _trkana->Branch((branch+"trkqual.mvaout").c_str(), &_allTQIs.at(i_branch).mvaout);
        _trkana->Branch((branch+"trkqual.mvastat").c_str(), &_allTQIs.at(i_branch).mvastat);
      }
      if (_conf.filltrkpid() && i_branchConfig.options().filltrkpid()) {
        int n_trkpid_vars = TrkCaloHitPID::n_vars;
        for (int i_trkpid_var = 0; i_trkpid_var < n_trkpid_vars; ++i_trkpid_var) {
          TrkCaloHitPID::MVA_varindex i_index =TrkCaloHitPID::MVA_varindex(i_trkpid_var);
          std::string varname = TrkCaloHitPID::varName(i_index);
          _trkana->Branch((branch+"trkpid."+varname).c_str(), &_allTPIs.at(i_branch)._tchpvars[i_index]);
        }
        _trkana->Branch((branch+"trkpid.mvaout").c_str(), &_allTPIs.at(i_branch)._mvaout);
        _trkana->Branch((branch+"trkpid.mvastat").c_str(), &_allTPIs.at(i_branch)._mvastat);
        _trkana->Branch((branch+"trkpid.disk0frad").c_str(), &_allTPIs.at(i_branch)._diskfrad[0]);
        _trkana->Branch((branch+"trkpid.disk1frad").c_str(), &_allTPIs.at(i_branch)._diskfrad[1]);
        _trkana->Branch((branch+"trkpid.disk0brad").c_str(), &_allTPIs.at(i_branch)._diskbrad[0]);
        _trkana->Branch((branch+"trkpid.disk1brad").c_str(), &_allTPIs.at(i_branch)._diskbrad[1]);
      }
      // optionally add hit-level branches
      // (for the time being diagLevel : 2 will still work, but I propose removing this at some point)
      if(_conf.diag() > 1 || (_conf.fillhits() && i_branchConfig.options().fillhits())){
        _trkana->Branch((branch+"tsh.").c_str(),&_allTSHIs.at(i_branch),_buffsize,_splitlevel);
        _trkana->Branch((branch+"tsm.").c_str(),&_allTSMIs.at(i_branch),_buffsize,_splitlevel);
      }
      // want to be able to have more than one bestcrv branch for each candidate/supplement
      if(_crv && i_branchConfig.options().fillbestcrv()) {
        std::vector<std::string> bestCrvBranchNames;
        if (i_branchConfig.options().bestCrvBranches(bestCrvBranchNames)) {
          for (size_t i_bestCrvBranch = 0; i_bestCrvBranch < bestCrvBranchNames.size(); ++i_bestCrvBranch) {
            std::string bestCrvBranchName = bestCrvBranchNames.at(i_bestCrvBranch);
            _trkana->Branch((branch+bestCrvBranchName+".").c_str(),&_allBestCrvs.at(i_branch).at(i_bestCrvBranch),_buffsize,_splitlevel);
            if (_fillmc) {
              _trkana->Branch((branch+bestCrvBranchName+"mc.").c_str(),&_allBestCrvMCs.at(i_branch).at(i_bestCrvBranch),_buffsize,_splitlevel);
            }
          }
        }
      }

      // optionally add MC branches
      if(_fillmc && i_branchConfig.options().fillmc()){
        _trkana->Branch((branch+"mc.").c_str(),&_allMCTIs.at(i_branch),_buffsize,_splitlevel);

        _trkana->Branch((branch+"mcsim.").c_str(),&_allMCSimTIs.at(i_branch),_buffsize,_splitlevel);
        _trkana->Branch((branch+"mcvd.").c_str(),&_allMCVDInfos.at(i_branch),_buffsize,_splitlevel);
        if(_fillcalomc)_trkana->Branch((branch+"tchmc.").c_str(),&_allMCTCHIs.at(i_branch),_buffsize,_splitlevel);
        // at hit-level MC information
        // (for the time being diagLevel will still work, but I propose removing this at some point)
        if(_conf.diag() > 1 || (_conf.fillhits() && i_branchConfig.options().fillhits())){
          _trkana->Branch((branch+"tshmc.").c_str(),&_allTSHIMCs.at(i_branch),_buffsize,_splitlevel);
        }
        // configure extra MCStep branches for this candidate
        if(_conf.extraMCStepTags(_extraMCStepTags)){
          auto& mcsics = _extraMCStepInfos.at(i_branch);
          auto& mcssis = _extraMCStepSummaryInfos.at(i_branch);
          for(size_t ixtra=0;ixtra < _extraMCStepTags.size(); ++ixtra) {
            auto& mcsic = mcsics.at(ixtra);
            auto& mcssi = mcssis.at(ixtra);
            auto const& tag = _extraMCStepTags[ixtra];
            auto inst = tag.instance();
            std::string  mcsiname = branch +"mcsic_" + inst;
            std::string  mcssiname = branch + "mcssi_" + inst;
            _trkana->Branch(mcsiname.c_str(),mcsic,_buffsize,_splitlevel);
            _trkana->Branch(mcssiname.c_str(),mcssi,_buffsize,_splitlevel);
          }
        }
      }
    }

    // trigger info.  Actual names should come from the BeginRun object FIXME
    if(_conf.filltrig()) {
      _trkana->Branch("trigbits",&_trigbits,_buffsize,_splitlevel);
    }
    // calorimeter information for the downstream electron track
    // general CRV info
    if(_crv) {
      if (_crvhits) {
        _trkana->Branch("crvsummary.",&_crvsummary,_buffsize,_splitlevel);
        _trkana->Branch("crvinfo.",&_crvinfo,_buffsize,_splitlevel);
        if(_crvpulses) {
          _trkana->Branch("crvpulseinfo.",&_crvpulseinfo,_buffsize,_splitlevel);
          _trkana->Branch("crvwaveforminfo.",&_crvwaveforminfo,_buffsize,_splitlevel);
        }
      }

      if(_fillmc){
        if (_crvhits) {
          _trkana->Branch("crvsummarymc.",&_crvsummarymc,_buffsize,_splitlevel);
          _trkana->Branch("crvinfomc.",&_crvinfomc,_buffsize,_splitlevel);
          _trkana->Branch("crvinfomcplane.",&_crvinfomcplane,_buffsize,_splitlevel);
          if(_crvpulses) {
            _trkana->Branch("crvpulseinfomc.",&_crvpulseinfomc,_buffsize,_splitlevel);
          }
        }
      }
    }
    // helix info
    if(_conf.helices()) _trkana->Branch("helixinfo.",&_hinfo,_buffsize,_splitlevel);
  }

  void TrkAnaTreeMaker::beginSubRun(const art::SubRun & subrun ) {
    // get bfield
    _infoStructHelper.updateSubRun();
  }

  void TrkAnaTreeMaker::analyze(const art::Event& event) {
    // reset event level structs
    _einfo.reset();
    _einfomc.reset();
    _hcnt.reset();
    _tcnt.reset();
    _hinfo.reset();
    _wtinfo.reset();
    // update timing maps for MC
    if(_fillmc){
      _infoMCStructHelper.updateEvent(event);
    }
    // fill event level info
    fillEventInfo(event);

    // need to create and define the event weight branch here because we only now know the EventWeight creating modules that have been run through the Event
    std::vector<art::Handle<EventWeight> > eventWeightHandles;
    _wtHandles = createSpecialBranch(event, "evtwt", eventWeightHandles, _wtinfo, _wtinfo._weights, false);

    std::string process = _conf.trigProcessName();
    // Get the KalSeedCollections for both the candidate and all supplements
    _allKSCHs.clear();
    _allRQCHs.clear();
    _allTQCHs.clear();
    _allTCHPCHs.clear();
    _allBestCrvAssns.clear();

    art::Handle<KalHelixAssns> khaH;
    if(_conf.helices()){ // find associated Helices
      BranchConfig i_branchConfig = _allBranches.at(0);
      art::InputTag kalSeedInputTag = i_branchConfig.input() + i_branchConfig.suffix();
      event.getByLabel(kalSeedInputTag,khaH);
    }

    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      BranchConfig i_branchConfig = _allBranches.at(i_branch);
      art::Handle<KalSeedCollection> kalSeedCollHandle;
      art::InputTag kalSeedInputTag = i_branchConfig.input() + i_branchConfig.suffix();
      event.getByLabel(kalSeedInputTag,kalSeedCollHandle);
      _allKSCHs.push_back(kalSeedCollHandle);

      // also create the reco qual branches
      std::vector<art::Handle<RecoQualCollection> > recoQualCollHandles;
      std::vector<art::Handle<RecoQualCollection> > selectedRQCHs;
      selectedRQCHs = createSpecialBranch(event, i_branchConfig.branch()+"qual", recoQualCollHandles, _allRQIs.at(i_branch), _allRQIs.at(i_branch)._qualsAndCalibs, true, i_branchConfig.suffix());
      for (const auto& i_selectedRQCH : selectedRQCHs) {
        if (i_selectedRQCH->size() != kalSeedCollHandle->size()) {
          throw cet::exception("TrkAna") << "Sizes of KalSeedCollection and this RecoQualCollection are inconsistent (" << kalSeedCollHandle->size() << " and " << i_selectedRQCH->size() << " respectively)";
        }
      }
      _allRQCHs.push_back(selectedRQCHs);

      // TrkQual
      std::string i_trkqual_tag;
      art::Handle<TrkQualCollection> trkQualCollHandle;
      if (i_branchConfig.options().trkqual(i_trkqual_tag) && i_branchConfig.options().filltrkqual() && _conf.filltrkqual()) {
        art::InputTag trkQualInputTag = i_trkqual_tag + i_branchConfig.suffix();
        event.getByLabel(trkQualInputTag,trkQualCollHandle);
        if (trkQualCollHandle->size() != kalSeedCollHandle->size()) {
          throw cet::exception("TrkAna") << "Sizes of KalSeedCollection and TrkQualCollection are inconsistent (" << kalSeedCollHandle->size() << " and " << trkQualCollHandle->size() << " respectively)";
        }
      }
      _allTQCHs.push_back(trkQualCollHandle);

      // TrkCaloHitPID
      std::string i_trkpid_tag;
      art::Handle<TrkCaloHitPIDCollection> trkpidCollHandle;
      if (i_branchConfig.options().trkpid(i_trkpid_tag) && i_branchConfig.options().filltrkpid() && _conf.filltrkpid()) {
        art::InputTag trkpidInputTag = i_trkpid_tag + i_branchConfig.suffix();
        event.getByLabel(trkpidInputTag,trkpidCollHandle);
        if (trkpidCollHandle->size() != kalSeedCollHandle->size()) {
          throw cet::exception("TrkAna") << "Sizes of KalSeedCollection and TrkCaloHitPIDCollection are inconsistent (" << kalSeedCollHandle->size() << " and " << trkpidCollHandle->size() << " respectively)";
        }
      }
      _allTCHPCHs.push_back(trkpidCollHandle);

      // BestCrv
      if (i_branchConfig.options().fillbestcrv() && _crv) { // if we are filling in bestcrv information
        std::vector<std::string> i_bestcrv_tags;
        std::vector<std::string> i_bestcrv_instances;
        std::vector<art::Handle<BestCrvAssns>> bestCrvAssnsHandles;
        if (i_branchConfig.options().bestCrvModules(i_bestcrv_tags) && i_branchConfig.options().bestCrvInstances(i_bestcrv_instances)) { // get the module labels and instances names
          // loop htrough the module lables
          art::Handle<BestCrvAssns> bestCrvAssnsHandle;
          for (size_t i_bestCrvBranch = 0; i_bestCrvBranch < i_bestcrv_tags.size(); ++i_bestCrvBranch) {
            art::InputTag bestCrvInputTag = i_bestcrv_tags.at(i_bestCrvBranch) + i_branchConfig.suffix() + ":" + i_bestcrv_instances.at(i_bestCrvBranch);
            event.getByLabel(bestCrvInputTag,bestCrvAssnsHandle); // get the Assns for this module and this candidate/supplement
            bestCrvAssnsHandles.push_back(bestCrvAssnsHandle); // add this handle to the vector of handles for this candidate/supplement
          }
        }
        _allBestCrvAssns[i_branch] = bestCrvAssnsHandles;
        if (_fillmc) {
          event.getByLabel(_crvMCAssnsTag, _crvMCAssnsHandle);
        }
      }
    }

    // trigger information
    if(_conf.filltrig()){
      fillTriggerBits(event,process);
    }
    // MC data
    if(_fillmc) { // get MC product collections
      event.getByLabel(_conf.primaryParticleTag(),_pph);
      event.getByLabel(_conf.kalSeedMCTag(),_ksmcah);
      if(_fillcalomc)event.getByLabel(_conf.caloClusterMCTag(),_ccmcch);
    }
    // fill track counts
    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      _tcnt._counts[i_branch] = (_allKSCHs.at(i_branch))->size();
    }

    // find extra MCStep collections
    _extraMCStepCollections.clear();
    for(size_t ixt = 0; ixt < _extraMCStepTags.size(); ixt++){
      auto const& tag = _extraMCStepTags[ixt];
      art::Handle<StepPointMCCollection> mcstepch;
      event.getByLabel(tag,mcstepch);
      _extraMCStepCollections.push_back(mcstepch);
    }

    // loop through all candidate tracks
    const auto& candidateKSCH = _allKSCHs.at(_candidateIndex);
    const auto& candidateKSC = *candidateKSCH;
    for (size_t i_kseed = 0; i_kseed < candidateKSC.size(); ++i_kseed) {
      resetTrackBranches(); // reset track branches here so that we don't get information from previous tracks in the next entry

      bool skip_kseed = false; // there may be a reason we don't want to write this KalSeed out

      auto const& candidateKS = candidateKSC.at(i_kseed);
      fillAllInfos(candidateKSCH, _candidateIndex, i_kseed); // fill the info structs for the candidate
      if(_conf.helices()){
        auto const& khassns = khaH.product();
        // find the associated HelixSeed to this KalSeed using the assns.
        auto hptr = (*khassns)[i_kseed].second;
        _infoStructHelper.fillHelixInfo(hptr, _hinfo);
      }


      // Now loop through all the branches (both candidate + supplements)...
      for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
        if (i_branch == _candidateIndex) { // ...but actually ignore candidate
          continue;
        }
        // check if supplement input collection is the same as the candidate input collections
        bool sameColl = false;
        if ( (_allBranches.at(_candidateIndex).input()+_allBranches.at(_candidateIndex).suffix())
            == (_allBranches.at(i_branch).input()+_allBranches.at(i_branch).suffix()) ) {
          sameColl = true;
        }
        const auto& i_supplementKSCH = _allKSCHs.at(i_branch);
        const auto& i_supplementKSC = *i_supplementKSCH;

        // If we require a supplement track of this type, and there are none...
        if (i_supplementKSC.size()==0 && _allBranches.at(i_branch).options().required()) {
          skip_kseed = true; // ...skip this KalSeed
        }

        // find the supplement track closest in time
        auto i_supplementKS = findSupplementTrack(i_supplementKSC,candidateKS,sameColl);
        if(i_supplementKS < i_supplementKSC.size()) {
          fillAllInfos(_allKSCHs.at(i_branch), i_branch, i_supplementKS);
        }
      }

      if (skip_kseed) {
        continue;
      }

      // TODO we want MC information when we don't have a track
      // fill general CRV info
      if(_crv){
        if (_crvhits) {
          CRVAnalysis::FillCrvHitInfoCollections(_crvCoincidenceModuleLabel, _crvCoincidenceMCModuleLabel,
              _crvRecoPulseLabel, _crvStepLabel, _conf.simParticleLabel(),
              _conf.mcTrajectoryLabel(), event, _crvinfo, _crvinfomc,
              _crvsummary, _crvsummarymc, _crvinfomcplane, _crvPlaneY);
        }
        if(_crvpulses){
          CRVAnalysis::FillCrvPulseInfoCollections(_crvRecoPulseLabel, _crvWaveformsModuleLabel, _crvDigiModuleLabel,
              event, _crvpulseinfo, _crvpulseinfomc, _crvwaveforminfo);
        }
      }
      // fill this row in the TTree
      _trkana->Fill();
    }

    if(_conf.pempty() && candidateKSC.size()==0) { // if we want to process empty events
      _trkana->Fill();
    }
  }

  size_t TrkAnaTreeMaker::findSupplementTrack(KalSeedCollection const& kcol,const KalSeed& candidate, bool sameColl) {
    size_t retval = kcol.size();

    // loop over supplement tracks and find the closest
    double candidate_time = candidate.t0().t0();
    double closest_time = 999999999;
    for(auto i_kseed=kcol.begin(); i_kseed != kcol.end(); i_kseed++) {
      double supplement_time = i_kseed->t0().t0();
      if( fabs(supplement_time - candidate_time) < fabs(closest_time-candidate_time)) {
        if (sameColl && fabs(supplement_time - candidate_time)<1e-5) {
          continue; // don't want the exact same track
        }
        closest_time = supplement_time;
        retval = i_kseed - kcol.begin();
      }
    }
    return retval;
  }

  void TrkAnaTreeMaker::fillEventInfo( const art::Event& event) {
    // fill basic event information
    _einfo.eventid = event.event();
    _einfo.runid = event.run();
    _einfo.subrunid = event.subRun();
    // currently no reco nproton estimate TODO

    auto PBThandle = event.getValidHandle<mu2e::ProtonBunchTime>(_PBTTag);
    auto PBT = *PBThandle;
    _einfo.pbtime = PBT.pbtime_;
    _einfo.pbterr = PBT.pbterr_;

    auto PBTMChandle = event.getValidHandle<mu2e::ProtonBunchTimeMC>(_PBTMCTag);
    auto PBTMC = *PBTMChandle;
    _einfomc.pbtime = PBTMC.pbtime_;

    auto PBIhandle = event.getValidHandle<mu2e::ProtonBunchIntensity>(_PBITag);
    auto PBI = *PBIhandle;
    _einfomc.nprotons = PBI.intensity();

    // get event weight products
    std::vector<Float_t> weights;
    for (const auto& i_weightHandle : _wtHandles) {
      double weight = i_weightHandle->weight();
      weights.push_back(weight);
    }
    _wtinfo.setWeights(weights);
  }

  void TrkAnaTreeMaker::fillTriggerBits(const art::Event& event,std::string const& process) {
    //get the TriggerResult from the process that created the KalFinalFit downstream collection
    art::InputTag const tag{Form("TriggerResults::%s", process.c_str())};
    auto trigResultsH = event.getValidHandle<art::TriggerResults>(tag);
    const art::TriggerResults* trigResults = trigResultsH.product();
    TriggerResultsNavigator tnav(trigResults);
    _trigbits = 0;
    // setup the bin labels
    unsigned ntrig(0);
    unsigned npath = trigResults->size();
    for(size_t ipath=0;ipath < npath; ++ipath){
      if (tnav.getTrigPath(ipath).find(_conf.trigpathsuffix()) != std::string::npos) {
        _tmap[ipath] = ntrig;
        ntrig++;
      }
    }
    for(size_t ipath=0;ipath < trigResults->size(); ++ipath){
      if(trigResults->accept(ipath)) {
        auto ifnd = _tmap.find(ipath);
        if(ifnd != _tmap.end()){
          unsigned itrig = ifnd->second;
          _trigbits |= 1 << itrig;
          if(_conf.debug() > 1)
            cout << "Trigger path " << tnav.getTrigPath(ipath) << " Trigger ID " << itrig << " returns " << trigResults->accept(ipath) << endl;
        }
      }
    }
    if(_conf.debug() > 0){
      cout << "Found TriggerResults for process " << process << " with " << trigResults->size() << " Lines"
        << " trigger bits word " << _trigbits << endl;
      if(_conf.debug() > 1){
        TriggerResultsNavigator tnav(trigResults);
        tnav.print();
      }
    }
  }

  void TrkAnaTreeMaker::fillAllInfos(const art::Handle<KalSeedCollection>& ksch, BranchIndex i_branch, size_t i_kseed) {

    const auto& kseed = ksch->at(i_kseed);
    // general info
    _infoStructHelper.fillTrkInfo(kseed,_allTIs.at(i_branch));

    // fit information at specific points:e

    _infoStructHelper.fillTrkFitInfo(kseed,_allTFIs.at(i_branch));
    if(_ftype == LoopHelix && kseed.loopHelixFit())_infoStructHelper.fillLoopHelixInfo(kseed,_allLHIs.at(i_branch));
    if(_ftype == CentralHelix && kseed.centralHelixFit())_infoStructHelper.fillCentralHelixInfo(kseed,_allCHIs.at(i_branch));
    if(_ftype == KinematicLine && kseed.kinematicLineFit())_infoStructHelper.fillKinematicLineInfo(kseed,_allKLIs.at(i_branch));
    BranchConfig branchConfig = _allBranches.at(i_branch);
    if(_conf.diag() > 1 || (_conf.fillhits() && branchConfig.options().fillhits())){ // want hit level info
      _infoStructHelper.fillHitInfo(kseed, _allTSHIs.at(i_branch));
      _infoStructHelper.fillMatInfo(kseed, _allTSMIs.at(i_branch));
    }

    // calorimeter info
    if (kseed.hasCaloCluster()) {
      _infoStructHelper.fillCaloHitInfo(kseed,  _allTCHIs.at(i_branch));
      _tcnt._ndec = 1; // only 1 possible calo hit at the moment
      // test
      if(_conf.debug()>0){
        auto const& tch = kseed.caloHit();
        auto const& cc = tch.caloCluster();
        std::cout << "CaloCluster has energy " << cc->energyDep()
          << " +- " << cc->energyDepErr() << std::endl;
      }
    }

    // BestCrv branches
    if (_crv && branchConfig.options().fillbestcrv()) {
      // get the list of bestcrv modules
      std::vector<std::string> i_bestcrv_tags;
      if (branchConfig.options().bestCrvModules(i_bestcrv_tags)) { // get the module labels
        for (size_t i_bestCrvBranch = 0; i_bestCrvBranch < i_bestcrv_tags.size(); ++i_bestCrvBranch) {
          auto hBestCrvAssns = _allBestCrvAssns.at(i_branch).at(i_bestCrvBranch);
          if (hBestCrvAssns->size()>0) {
            auto bestCrvCoinc = hBestCrvAssns->at(i_kseed).second;
            _infoStructHelper.fillCrvHitInfo(bestCrvCoinc, _allBestCrvs.at(i_branch).at(i_bestCrvBranch));
            if (_fillmc) {
              // loop through data-MC assns and find bestCrvCoinc (which is an art::Ptr){
              for (const auto& i_crvMCAssn : *_crvMCAssnsHandle) {
                if (bestCrvCoinc == i_crvMCAssn.first) {
                  auto bestCrvCoincMC = i_crvMCAssn.second;//art::Ptr<CrvCoincidenceClusterMC>(_crvCoincidenceMCCollHandle, bestCrvCoinc.key());
                  _infoMCStructHelper.fillCrvHitInfoMC(bestCrvCoincMC, _allBestCrvMCs.at(i_branch).at(i_bestCrvBranch));
                  break;
                }
              }
            }
          }
        }
      }
    }

    // all RecoQuals
    std::vector<Float_t> recoQuals; // for the output value
    for (const auto& i_recoQualHandle : _allRQCHs.at(i_branch)) {
      Float_t recoQual = i_recoQualHandle->at(i_kseed)._value;
      recoQuals.push_back(recoQual);
      Float_t recoQualCalib = i_recoQualHandle->at(i_kseed)._calib;
      recoQuals.push_back(recoQualCalib);
    }
    _allRQIs.at(i_branch).setQuals(recoQuals);
    // TrkQual
    std::string trkqual_branch;
    if(_conf.filltrkqual() && branchConfig.options().filltrkqual() && branchConfig.options().trkqual(trkqual_branch)) {
      const auto& trkQualCollHandle = _allTQCHs.at(i_branch);
      if (trkQualCollHandle.isValid()) { // we could have put an empty TrkQualCollection in, if we didn't want it
        const auto& trkQualColl = *trkQualCollHandle;
        const auto& trkQual = trkQualColl.at(i_kseed);
        _infoStructHelper.fillTrkQualInfo(trkQual, _allTQIs.at(i_branch));
      }
    }
    // TrkCaloHitPID
    std::string trkpid_branch;
    if (_conf.filltrkpid() && branchConfig.options().filltrkpid() && branchConfig.options().trkpid(trkpid_branch)) {
      const auto& tchpcolH = _allTCHPCHs.at(i_branch);
      if (tchpcolH.isValid()) {
        const auto& tchpcol = *tchpcolH;
        auto const& tpid = tchpcol.at(i_kseed);
        _infoStructHelper.fillTrkPIDInfo(tpid, kseed, _allTPIs.at(i_branch));
      }
    }
    // fill MC info associated with this track
    if(_fillmc && branchConfig.options().fillmc()) {
      const PrimaryParticle& primary = *_pph;
      // use Assns interface to find the associated KalSeedMC; this uses ptrs
      auto kptr = art::Ptr<KalSeed>(ksch,i_kseed);
      //        std::cout << "KalSeedMCMatch has " << _ksmcah->size() << " entries" << std::endl;
      for(auto iksmca = _ksmcah->begin(); iksmca!= _ksmcah->end(); iksmca++){
        //        std::cout << "KalSeed Ptr " << kptr << " match Ptr " << iksmca->first << std::endl;
        if(iksmca->first == kptr) {
          auto const& kseedmc = *(iksmca->second);
          auto const& kseed = *kptr;
          _infoMCStructHelper.fillTrkInfoMC(kseed, kseedmc, _allMCTIs.at(i_branch));
          auto& mcvdis = _allMCVDInfos.at(i_branch);
          _infoMCStructHelper.fillVDInfo(kseed, kseedmc, mcvdis);
          _infoMCStructHelper.fillAllSimInfos(kseedmc, primary, _allMCSimTIs.at(i_branch), branchConfig.options().genealogyDepth());

          if(_conf.diag() > 1 || (_conf.fillhits() && branchConfig.options().fillhits())){
            _infoMCStructHelper.fillHitInfoMCs(kseedmc, _allTSHIMCs.at(i_branch));
          }
          // fill extra MCStep info for this branch
          for(size_t ixt = 0; ixt < _extraMCStepTags.size(); ixt++){
            auto const& mcsc = *_extraMCStepCollections[ixt];
            auto& mcsic = _extraMCStepInfos.at(i_branch).at(ixt);
            auto& mcssi = _extraMCStepSummaryInfos.at(i_branch).at(ixt);
            _infoMCStructHelper.fillExtraMCStepInfos(kseedmc,mcsc,*mcsic,*mcssi);
          }
          break;
        }
      }
      if (kseed.hasCaloCluster() && _fillcalomc) {
        // fill MC truth of the associated CaloCluster.  Use the fact that these are correlated by index with the clusters in that collection
        auto index = kseed.caloCluster().key();
        auto const& ccmcc = *_ccmcch;
        auto const& ccmc = ccmcc[index];
        _infoMCStructHelper.fillCaloClusterInfoMC(ccmc,_allMCTCHIs.at(i_branch));  // currently broken due to CaloMC changes.  This needs fixing in compression
      }
    }
  }

  // some branches can't be made until the analyze() function because we want to write out all data products of a certain type
  // these all have an underlying array where we want to name the individual elements in the array with different leaf names
  template <typename T, typename TI, typename TIA>
  std::vector<art::Handle<T> >  TrkAnaTreeMaker::createSpecialBranch(const art::Event& event, const std::string& branchname,
  std::vector<art::Handle<T> >& handles, // this parameter is only needed so that the template parameter T can be deduced. There is probably a better way to do this FIXME
  TI& infostruct, TIA& array, bool make_individual_branches, const std::string& selection) {
    std::vector<art::Handle<T> > outputHandles;
    std::vector<art::Handle<T> > inputHandles = event.getMany<T>();
    if (inputHandles.size()>0) {
      std::vector<std::string> labels;
      for (const auto& i_handle : inputHandles) {
        std::string moduleLabel = i_handle.provenance()->moduleLabel();
        // event.getMany() doesn't have a way to wildcard part of the ModuleLabel, do it ourselves here
        size_t pos;
        if (selection != "") { // if we want to add a selection
          pos = moduleLabel.find(selection);

          // make sure that the selection (e.g. "DeM") appears at the end of the module label
          if (pos == std::string::npos) {
            //      std::cout << "Selection not found" << std::endl;
            continue;
          }
          else if (pos+selection.length() != moduleLabel.size()) {
            //      std::cout << "Selection wasn't at end of moduleLabel" << std::endl;
            continue;
          }
          moduleLabel = moduleLabel.erase(pos, selection.length());
        }
        std::string instanceName = i_handle.provenance()->productInstanceName();

        std::string branchname = moduleLabel;
        if (instanceName != "") {
          branchname += "_" + instanceName;
        }
        outputHandles.push_back(i_handle);
        labels.push_back(branchname);
      }
      if (make_individual_branches) { // if we want to make individual branches per leaf (e.g. to avoid branch ambiguities in python such as detrkqual.NActiveHits vs uetrkqual.NActiveHits)
        const std::vector<std::string>& leafnames = infostruct.leafnames(labels);
        int n_leaves = leafnames.size();
        for (int i_leaf = 0; i_leaf < n_leaves; ++i_leaf) {
          std::string thisbranchname = (branchname+"."+leafnames.at(i_leaf));
          if (!_trkana->GetBranch(thisbranchname.c_str())) {  // only want to create the branch once
            _trkana->Branch(thisbranchname.c_str(), &array[i_leaf]);
          }
        }
      }
      else {
        if (!_trkana->GetBranch((branchname+".").c_str())) {  // only want to create the branch once
          _trkana->Branch((branchname+".").c_str(), &infostruct, infostruct.leafname(labels).c_str());
        }
      }
    }
    return outputHandles;
  }

  void TrkAnaTreeMaker::resetTrackBranches() {
    for (BranchIndex i_branch = 0; i_branch < _allBranches.size(); ++i_branch) {
      _allTIs.at(i_branch).reset();
      _allTFIs.at(i_branch).assign(_allTFIs.at(i_branch).size(), TrkFitInfo());       // we don't want to remove elements and have to use push_back again, so use assign instead of clear to put in empty TrkFitInfo struct

      _allTCHIs.at(i_branch).reset();

      _allMCTIs.at(i_branch).reset();
      _allMCSimTIs.at(i_branch).clear();       // we do want to remove elements since we may have different numbers of SimInfos

      if(_fillcalomc)_allMCTCHIs.at(i_branch).reset();

      _allRQIs.at(i_branch).reset();
      _allTQIs.at(i_branch).reset();
      _allTPIs.at(i_branch).reset();

      // clear vectors
      _allTSHIs.at(i_branch).clear();
      _allTSMIs.at(i_branch).clear();
      _allTSHIMCs.at(i_branch).clear();

      if (_allBranches.at(i_branch).options().fillbestcrv()) { // only clear the vectors if they exist
        // we don't want to remove elements so use assign instead of clear
        _allBestCrvs.at(i_branch).assign(_allBestCrvs.at(i_branch).size(), CrvHitInfoReco());
        _allBestCrvMCs.at(i_branch).assign(_allBestCrvMCs.at(i_branch).size(), CrvHitInfoMC());
      }
    }
    // clear vectors
    _crvinfo.clear();
    _crvinfomc.clear();
    _crvinfomcplane.clear();
    _crvpulseinfo.clear();
    _crvwaveforminfo.clear();
    _crvpulseinfomc.clear();
  }
}  // end namespace mu2e

// Part of the magic that makes this class a module.
// create an instance of the module.  It also registers
using mu2e::TrkAnaTreeMaker;
DEFINE_ART_MODULE(TrkAnaTreeMaker)
