
#include "GtPlusReconJob2DTGadget.h"
#include "GtPlusGadgetOpenMP.h"

using namespace Gadgetron::gtPlus;

namespace Gadgetron
{

GtPlusReconJob2DTGadget::GtPlusReconJob2DTGadget() : mem_manager_(new Gadgetron::gtPlus::gtPlusMemoryManager(4, 640*1024*1024))
{
    debugFolder_ = "DebugOutput";

    performTiming_ = true;

    verboseMode_ = false;

    gt_timer1_.set_timing_in_destruction(false);
    gt_timer2_.set_timing_in_destruction(false);
    gt_timer3_.set_timing_in_destruction(false);

    process_config_called_ = false;

    Gadgetron::prepOpenMP();
    Gadgetron::prepMKL();
}

GtPlusReconJob2DTGadget::~GtPlusReconJob2DTGadget()
{

}

bool GtPlusReconJob2DTGadget::readParameters()
{
    try
    {
        GADGET_CONDITION_MSG(verboseMode_, "------> GtPlusReconJob2DTGadget parameters <------");

        boost::shared_ptr<std::string> str = this->get_string_value("debugFolder");
        debugFolder_ = *str;
        GADGET_CONDITION_MSG(verboseMode_, "debugFolder_ is " << debugFolder_);

        performTiming_ = this->get_bool_value("performTiming");
        GADGET_CONDITION_MSG(verboseMode_, "performTiming_ is " << performTiming_);

        GADGET_CONDITION_MSG(verboseMode_, "-----------------------------------------------");
    }
    catch(...)
    {
        GADGET_ERROR_MSG("Errors in GtPlusReconJob2DTGadget::readParameters() ... ");
        return false;
    }

    return true;
}

int GtPlusReconJob2DTGadget::process_config(ACE_Message_Block* mb)
{
    // [Ro E1 Cha Slice E2 Con Phase Rep Set Seg]
    //   0  1  2   3    4   5    6     7  8   9

    verboseMode_ = this->get_bool_value("verboseMode");

    // read in parameters from the xml
    GADGET_CHECK_RETURN(this->readParameters(), GADGET_FAIL);

    // generate the destination folder
    if ( !debugFolder_.empty() )
    {
        GADGET_CHECK_RETURN_FALSE(generateDebugFolderPath(debugFolder_, debugFolder_fullPath_));
    }
    else
    {
        GADGET_MSG("GtPlusRecon, debugFolder is not set ...");
    }

    GADGET_START_TIMING_CONDITION(gt_timer1_, "Pre-allocate memory ... ", performTiming_);
    mem_manager_->increase(4.0*1024*1024*1024);
    GADGET_STOP_TIMING_CONDITION(gt_timer1_, performTiming_);

    worker_grappa_.gtPlus_mem_manager_ = mem_manager_;
    worker_noacceleration_.gtPlus_mem_manager_ = mem_manager_;
    worker_spirit_.gtPlus_mem_manager_ = mem_manager_;
    worker_spirit_L1_ncg_.gtPlus_mem_manager_ = mem_manager_;

    return GADGET_OK;
}

int GtPlusReconJob2DTGadget::process(Gadgetron::GadgetContainerMessage< int >* m1, Gadgetron::GadgetContainerMessage< GtPlusReconJobTypeCPFL > * m2)
{
    // because the parameter configuration will not be sent, we need to call process_config explicitly
    if ( !process_config_called_ )
    {
        GADGET_CHECK_RETURN( (this->process_config(m1)==0), GADGET_FAIL);
        process_config_called_ = true;
    }
    GADGET_CONDITION_MSG(verboseMode_, "GtPlusReconJob2DTGadget::process(...) starts ... ");

    int* jobID = m1->getObjectPtr();
    GADGET_CONDITION_MSG(verboseMode_, "--> arriving job : " << *jobID << " ... ");

    GtPlusReconJobTypeCPFL* job = m2->getObjectPtr();
    GADGET_CONDITION_MSG(verboseMode_, "    job array size : [ " << job->kspace.get_size(0) << " " 
                                                                 << job->kspace.get_size(1) << " " 
                                                                 << job->kspace.get_size(2) << " " 
                                                                 << job->kspace.get_size(3) << " ] ... ");

    // set the worker
    worker_grappa_.performTiming_ = performTiming_;
    if ( !debugFolder_fullPath_.empty() ) worker_grappa_.debugFolder_ = debugFolder_fullPath_;

    worker_noacceleration_.performTiming_ = performTiming_;
    if ( !debugFolder_fullPath_.empty() ) worker_noacceleration_.debugFolder_ = debugFolder_fullPath_;

    worker_spirit_.performTiming_ = performTiming_;
    if ( !debugFolder_fullPath_.empty() ) worker_spirit_.debugFolder_ = debugFolder_fullPath_;

    worker_spirit_L1_ncg_.performTiming_ = performTiming_;
    if ( !debugFolder_fullPath_.empty() ) worker_spirit_L1_ncg_.debugFolder_ = debugFolder_fullPath_;

    if ( verboseMode_ )
    {
        job->workOrder2DT.print(std::cout);
    }

    bool succeed = true;
    GADGET_START_TIMING_CONDITION(gt_timer1_, "Recon 2DT job ... ", performTiming_);

    succeed = worker_spirit_L1_ncg_.performUnwarppingImpl(*job);

    GADGET_STOP_TIMING_CONDITION(gt_timer1_, performTiming_);

    // export the results
    if ( !debugFolder_fullPath_.empty() )
    {
        std::ostringstream ostr;
        ostr << "ReconJob2DT_ID" << *jobID;

        hoNDArray<GT_Complex8> res = job->res;
        res.squeeze();
        GADGET_EXPORT_ARRAY_COMPLEX(debugFolder_fullPath_, gt_exporter_, res, ostr.str());
    }

    // clean the kspace and ker and coil map
    job->kspace.clear();
    job->ker.clear();
    if ( !job->workOrder2DT.coilMap_ ) job->workOrder2DT.coilMap_->clear();

    if ( !succeed )
    {
        job->complexIm.clear();
        job->res.clear();
    }

    // send out the results
    GADGET_CHECK_RETURN(this->sendOutJob(*jobID, job), GADGET_FAIL);

    GADGET_CONDITION_MSG(verboseMode_, "GtPlusReconJob2DTGadget::process(...) ends ... ");

    m1->release();

    return GADGET_OK;
}

bool GtPlusReconJob2DTGadget::
sendOutJob(int jobID, GtPlusReconJobTypeCPFL* job)
{
    try
    {
        ACE_DEBUG( (LM_INFO, ACE_TEXT("GtPlusReconJob2DTGadget sendOutJob ... ")) );

        if (!this->controller_)
        {
            ACE_DEBUG( (LM_DEBUG, ACE_TEXT("Cannot return result to controller, no controller set")) );
            return false;
        }

        GadgetContainerMessage<GadgetMessageIdentifier>* mb =
            new GadgetContainerMessage<GadgetMessageIdentifier>();

        mb->getObjectPtr()->id = GADGET_MESSAGE_CLOUD_JOB;

        GadgetContainerMessage<int>* m1 = new GadgetContainerMessage<int>();
        *(m1->getObjectPtr()) = jobID;

        GadgetContainerMessage<GtPlusReconJobTypeCPFL>* m2 = new GadgetContainerMessage<GtPlusReconJobTypeCPFL>();

        *(m2->getObjectPtr()) = *job;

        m1->cont(m2);
        mb->cont(m1);

        int ret =  this->controller_->output_ready(mb);
        if (ret < 0)
        {
            GADGET_DEBUG1("Failed to return GtPlusReconJob2DTGadget job massage to controller\n");
            return false;
        }
    }
    catch(...)
    {
        GADGET_ERROR_MSG("Errors in GtPlusReconJob2DTGadget::sendOutJob(...) ... ");
        return false;
    }

    return true;
}

bool GtPlusReconJob2DTGadget::
    generateDebugFolderPath(const std::string& debugFolder, std::string& debugFolderPath)
{
    debugFolderPath = ACE_OS::getenv("GADGETRON_HOME");
    debugFolderPath.append("/");
    debugFolderPath.append(debugFolder);
    debugFolderPath.append("/");
    GADGET_CONDITION_MSG(verboseMode_, "Debug folder is " << debugFolderPath);
    return true;
}

void GtPlusReconJob2DTGadget::
    getCurrentMoment(std::string& procTime)
{
    char timestamp[100];
    time_t mytime;
    struct tm *mytm;
    mytime=time(NULL);
    mytm=localtime(&mytime);
    strftime(timestamp, sizeof(timestamp),"_%a_%d_%b_%Y_%H_%M_%S",mytm);
    procTime = timestamp;
}

GADGET_FACTORY_DECLARE(GtPlusReconJob2DTGadget)

}
