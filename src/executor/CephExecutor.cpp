/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CephExecutor.hpp"

#include <sys/wait.h>
#include <glog/logging.h>
#include <boost/lexical_cast.hpp>
#include <thread>
#include <time.h>

#include "common/FrameworkMessage.hpp"
#include "common/StringUtil.hpp"
#include "common/NetUtil.hpp"
#include "common/TaskType.hpp"
#include "httpserver/FileServer.hpp"

using ceph::TaskType;
using boost::lexical_cast;
using std::thread;

void CephExecutor::reregistered(
      ExecutorDriver* driver,
      const SlaveInfo& slaveInfo)
{
  LOG(INFO) << "reregisted";
}

void CephExecutor::disconnected(ExecutorDriver* driver){}

void CephExecutor::killTask(ExecutorDriver* driver, const TaskID& taskId){}

void CephExecutor::error(ExecutorDriver* driver, const string& message){}

void CephExecutor::deleteConfigDir(string localSharedConfigDir)
{
  string cmd = "rm -rf " +
      localSharedConfigDirRoot + "/" + localSharedConfigDir;
  string r = runShellCommand(cmd);
  LOG(INFO) << "Delete config dir with command: " << cmd;
}

bool CephExecutor::existsConfigFiles(string localSharedConfigDir)
{
  string cmd;
  string r;
  string pre = localSharedConfigDirRoot +
      "/" + localSharedConfigDir + "/etc/ceph/";
  string pre1 = localSharedConfigDirRoot +
      "/" + localSharedConfigDir + "/var/lib/ceph/";
  string adminkeyring = pre + "ceph.client.admin.keyring";
  string cephconf = pre + "ceph.conf";
  string monkeyring = pre + "ceph.mon.keyring";
  string monmap = pre + "monmap";
  string osdkeyring = pre1 + "bootstrap-osd/ceph.keyring";
  string rgwkeyring = pre1 + "bootstrap-rgw/ceph.keyring";
  string mdskeyring = pre1 + "bootstrap-mds/ceph.keyring";
  cmd = "[ -f "+ adminkeyring + " ]" + " && " +
      "[ -f "+ cephconf + " ]" + " && " +
      "[ -f "+ monkeyring + " ]" + " && " +
      "[ -f "+ monmap + " ]" + " && " +
      "[ -f "+ osdkeyring + " ]" + " && " +
      "[ -f "+ rgwkeyring + " ]" + " && " +
      "[ -f "+ mdskeyring + " ]" +
      ";echo $?";
  r = runShellCommand(cmd);
  //already have the shared config
  if ( "0" == r ) {
    return true;
  }
  return false;
}

bool CephExecutor::createLocalSharedConfigDir(string localSharedConfigDir)
{
  if (existsConfigFiles(localSharedConfigDir)) {
    LOG(INFO) << "Ceph config files already exists";
    return true;
  }
  string cmd = "cd " + localSharedConfigDirRoot + " && " +
      "mkdir " + localSharedConfigDir + " && " +
      "cd " + localSharedConfigDir + " && " +
      "mkdir -p ./etc/ceph && " +
      "mkdir -p ./var/lib/ceph/bootstrap-mds && " +
      "mkdir -p ./var/lib/ceph/bootstrap-rgw && " +
      "mkdir -p ./var/lib/ceph/bootstrap-osd && " +
      "mkdir -p ./var/lib/ceph/mds && " +
      "mkdir -p ./var/lib/ceph/osd && " +
      "mkdir -p ./var/lib/ceph/mon && " +
      "mkdir -p ./var/lib/ceph/radosgw && " +
      "mkdir -p ./var/lib/ceph/tmp && " +
      "cd .. && chmod 777 -R " + localSharedConfigDir + " && " +
      "echo $?";
  LOG(INFO) << "Using this command to create local shared config directory:";
  LOG(INFO) << cmd;
  string r = runShellCommand(cmd);
  if ("0" == r) {
    return true;
  } else {
    return false;
  }
}

bool CephExecutor::copySharedConfigFiles()
{
  string localMountDir = localSharedConfigDirRoot +                                                                                      
      "/" +localConfigDirName;
  string abpath1 = localMountDir + "/"
      + "etc/ceph/";
  string abpath2 = localMountDir + "/"
      + "var/lib/ceph/";
  string cmd = "cd " + sandboxAbsolutePath + " && " +
      "cp ./ceph.client.admin.keyring " +
      "./ceph.conf " +
      "./ceph.mon.keyring " +
      "./monmap " + abpath1 + " && " +
      "cp ./ceph.keyring.osd " + abpath2 + "bootstrap-osd/ceph.keyring" +
      " && " +
      "cp ./ceph.keyring.mds " + abpath2 + "bootstrap-mds/ceph.keyring" +
      " && " +
      "cp ./ceph.keyring.rgw " + abpath2 + "bootstrap-rgw/ceph.keyring" +
      ";echo $?";
  string r = runShellCommand(cmd);
  if ("0" == r){
    return true;
  } else {
    LOG(INFO) << cmd;
    return false;
  }
}

string CephExecutor::constructMonCommand(
      string localMountDir,
      string _containerName)
{
  string cmd;
  string imagename = "ceph/daemon mon";
  string local_config_dir = localMountDir + "/etc/ceph";
  string local_monfs_dir = localMountDir + "/var/lib/ceph";
  string docker_start_cmd = "docker run --rm --net=host --privileged --name " +
      _containerName;
  cmd = "hostname -I";
  string ips = runShellCommand(cmd);
  string ip = getPublicNetworkIP(ips,mgmtNIC);
  string docker_env_param = " -e MON_IP=" + ip;
  docker_env_param = docker_env_param + " -e OSD_JOURNAL_SIZE=0";
  docker_env_param = docker_env_param + " -e CEPH_PUBLIC_NETWORK=" + mgmtNIC;
  docker_env_param = docker_env_param + " -e CEPH_CLUSTER_NETWORK=" + dataNIC;
  //TODO: mount /etc/localtime means sync time with localhost, need at least Docker v1.6.2
  string docker_volume_param = " -v " + local_config_dir +
      ":/etc/ceph:rw" + " -v " + local_monfs_dir +
      ":/var/lib/ceph:rw ";
  string docker_command;
  docker_command.append(docker_start_cmd);
  docker_command.append(docker_env_param);
  docker_command.append(docker_volume_param);
  docker_command.append(imagename);
  return docker_command;
}

string CephExecutor::registerOSD()
{
  string cmd = "docker exec " + containerName +" ceph osd create";
  string osdID = runShellCommand(cmd);
  return osdID;
}

string CephExecutor::constructOSDCommand(
      string localMountDir,
      string osdId,
      string _containerName)
{
  string cmd;
  string imagename = "ceph/daemon osd";
  string local_config_dir = localMountDir + "/etc/ceph";
  string local_fs_dir = localMountDir + "/var/lib/ceph";
  string osdid_dir_name = "ceph-" + osdId;
  string jnlid_dir_name = "jnl-" + osdId;
  string docker_start_cmd = "docker run --rm --pid=host --net=host --privileged --name " +
      _containerName;
  string docker_env_param = " -e JOURNAL_DIR=/var/lib/ceph/osd/journal";
  docker_env_param += " -e OSD_TYPE=directory";
  //TODO: mount /etc/localtime means sync time with localhost, need at least Docker v1.6.2
  string docker_volume_param =
      " -v " + local_config_dir + ":/etc/ceph:rw" + // for mon keyring and client keyring
      " -v " + local_fs_dir + "/bootstrap-osd" +
          ":/var/lib/ceph/bootstrap-osd:rw" +// for bootstrap osd key
      " -v " + local_fs_dir + "/osd/" + osdid_dir_name +
          ":/var/lib/ceph/osd/" + osdid_dir_name + ":rw" +// for osd id
      " -v " + local_fs_dir + "/osd/" + jnlid_dir_name +
          ":/var/lib/ceph/osd/journal" + ":rw ";//for journal
  //another workaround: use --pid=host when start OSD
  string docker_command;
  docker_command.append(docker_start_cmd);
  docker_command.append(docker_env_param);
  docker_command.append(docker_volume_param);
  docker_command.append(imagename);
  return docker_command;
}

string CephExecutor::constructRADOSGWCommand(
    string localMountDir,
    string _containerName)
{
  string imagename = "ceph/daemon rgw";
  string local_config_dir = localMountDir + "/etc/ceph";
  string local_fs_dir = localMountDir + "/var/lib/ceph";
  string docker_start_cmd = "docker run --rm --net=host --privileged --name " +
      _containerName;
  string docker_env_param = " -e RGW_NAME=" + _containerName;
  //TODO: mount /etc/localtime means sync time with localhost, need at least Docker v1.6.2
  string docker_volume_param =
      " -v " + local_config_dir + ":/etc/ceph:rw" +
      " -v " + local_fs_dir + "/bootstrap-rgw" +
          ":/var/lib/ceph/bootstrap-rgw:rw ";// for bootstrap osd key
  string docker_command;
  docker_command.append(docker_start_cmd);
  docker_command.append(docker_env_param);
  docker_command.append(docker_volume_param);
  docker_command.append(imagename);
  return docker_command;

}

bool CephExecutor::block_until_started(string _containerName, string timeout)
{
  time_t current = time(0);
  time_t deadline = current + lexical_cast<int>(timeout);
  //poll the result every 3 seconds until timeout
  string cmd;
  string r;
  while (current < deadline) {
    cmd = "docker ps |grep " + _containerName + " > /dev/null ;echo $?";
    r = runShellCommand(cmd);
    usleep(3*1000000);
    if ("0" == r) {
      break;
    }
  }
  if ("0" != r) {
    LOG(INFO) << "Docker container failed to start!";
    return false;
  }
  cmd = "docker exec " + _containerName +
      " timeout "+ timeout + " ceph mon stat > /dev/null 2>&1; echo $?";
  r = runShellCommand(cmd);
  if ("0" != r){
    LOG(INFO) << "Container's ceph daemon failed to start! " << r;
    return false;
  }
  LOG(INFO) << "error code: " << r;
  return true;
}

void CephExecutor::startLongRunning(string binaryName, string cmd)
{
  if ("" != cmd) {
    vector<char*> tokens = StringUtil::explode(cmd,' ',0);
    tokens.push_back(NULL);
    if (execvp(binaryName.c_str(), &tokens[0]) < 0) {
      LOG(INFO) << "Failed to start this :" << cmd;
      exit(1);
    }
  } else {
    LOG(INFO) << "Empty command given, do nothing";
  }

}


bool CephExecutor::downloadDockerImage(string imageName)
{
  string cmd = "docker images|grep " + imageName +
      " > /dev/null 2>&1;echo $?";
  string r = runShellCommand(cmd);
  if ("0" == r) {
    LOG(INFO) << imageName <<" exists, so quit download";
    return true;
  }
  //TODO: fix me. Seems docker pull need proxy there.
  cmd = "docker pull " + imageName;
  LOG(INFO) << "Downloading " << imageName << " ...";
  LOG(INFO) << cmd;
  pid_t downloadPid = fork();
  if (-1 == downloadPid){
    LOG(ERROR) << "Failed to fork download thread";
  }
  if (0 == downloadPid){
    //child thread to download the docker
    startLongRunning("docker",cmd);
  } else {
    //parent ,wait util finish downloading
    int* status;
    waitpid(0,status,0);
    if(WIFEXITED(status)){
      LOG(INFO) << "Downloading done";
      return true;
    } else {
      LOG(INFO) << "Downloading done";
      return false;
    }
  }
  return false;
}

bool CephExecutor::parseHostConfig(TaskInfo taskinfo)
{
  if (taskinfo.has_labels()) {
    Labels labels = taskinfo.labels();
    int count = labels.labels_size();
    LOG(INFO) << "labels count: " <<count;
    if (0 == count) {
      LOG(ERROR) << "Zero host config data given";
      return false;
    }
    for (int i = 0; i < count; i++) {
      Label* one = labels.mutable_labels(i);
      if (0 == strcmp(CustomData::mgmtdevKey,one->key().c_str())) {
        LOG(INFO) << "got mgmt NIC: " <<one->value();
        mgmtNIC = one->value();
      }
      if (0 == strcmp(CustomData::datadevKey,one->key().c_str())) {
        LOG(INFO) << "got data NIC: " <<one->value();
        dataNIC = one->value();
      }
      if (0 == strcmp(CustomData::osddevsKey,one->key().c_str())) {
        LOG(INFO) << "got osd dev: " <<one->value();
        osddisk = one->value();
      }
      if (0 == strcmp(CustomData::jnldevsKey,one->key().c_str())) {
        LOG(INFO) << "got jnl dev: " <<one->value();
        jnldisk = one->value();
      }
    }
    return true;
  } else {
    LOG(ERROR)<<"No labels found in taskinfo!";
    return false;
  }
}

bool CephExecutor::prepareDisks()
{
  //create the osd mount dir
  string cmd = "mkdir -p " + localMountOSDDir;
  runShellCommand(cmd);
  //create the jnl mount dir
  cmd = "mkdir -p " + localMountJNLDir;
  runShellCommand(cmd);
  bool r;
  if ("" == osddisk) {
    LOG(INFO) << "No osd disk given, use system disk for osd";
  } else {
    r = mountDisk("/dev/" + osddisk, localMountOSDDir, mountFLAGS);
    if (!r) {
      LOG(INFO) << "Mount osd disk failed!";
      return false;
    }
  }
  if ("" == jnldisk) {
    LOG(INFO) << "No jnl disk given, use system disk for journal";
  } else {
    r = mountDisk("/dev/" + jnldisk, localMountJNLDir, mountFLAGS);
    if (!r) {
      LOG(INFO) << "Mount jnl disk failed!";
      return false;
    }
  }
  return true;
}

bool CephExecutor::mountDisk(string diskname, string dir, string flags)
{
  string cmd = "mount" + flags + " " + diskname + " " + dir;
  LOG(INFO) << "mount..( "<<cmd<<" )";
  runShellCommand(cmd);
  return true;
}

string CephExecutor::getPublicNetworkIP(string ips, string CIDR)
{
  vector<string> tokens = StringUtil::explode(ips,' ');
  for (size_t i = 0; i < tokens.size(); i++) {
    if (StringUtil::matchIPToCIDR(tokens[i],CIDR)) {
      return tokens[i];
    }
  }
  return runShellCommand("hostname -i");
}

string CephExecutor::getContainerName(string taskId)
{
  vector<string> tokens = StringUtil::explode(taskId,'.');
  return tokens[0];
}
// data format is:
// <MessageToExecutor>.<OSDID> for OSD executor
// or just <MessageToExecutor> for MON executor
void CephExecutor::frameworkMessage(ExecutorDriver* driver, const string& data)
{

  LOG(INFO) << "Got framework message: " << data;
  MessageToExecutor msg;
  vector<string> tokens = StringUtil::explode(data,'.');
  msg = (MessageToExecutor)lexical_cast<int>(tokens[0]);
  switch (msg){
    case MessageToExecutor::REGISTER_OSD:
      LOG(INFO) << "Will register an OSD, and return the OSD ID";
      driver->sendFrameworkMessage(registerOSD());
      break;
    case MessageToExecutor::LAUNCH_OSD:
      if (tokens.size() == 2){
        LOG(INFO) << "Will launch OSD docker with OSD ID: " << tokens[1];
        string localMountDir = localSharedConfigDirRoot +
            "/" + localConfigDirName;
        localMountOSDDir = localMountDir +
            "/var/lib/ceph/osd/ceph-" + tokens[1];
        localMountJNLDir = localMountDir +
            "/var/lib/ceph/osd/jnl-" + tokens[1];
        TaskStatus status;
        status.mutable_task_id()->MergeFrom(myTaskId);
        //prepareOSDdisk and JournalDisk
        if (!prepareDisks()) {
          LOG(ERROR) << "Prepare Disks failed!";
          status.set_state(TASK_FAILED);
          driver->sendStatusUpdate(status);
          return;
        }
        string dockerCommand = constructOSDCommand(
            localMountDir,
            tokens[1],
            containerName);

        LOG(INFO) << "Stating OSD container with command: ";
        LOG(INFO) << dockerCommand;
        myPID = fork();
        if (0 == myPID){
            //child long running docker thread
            //TODO: we use fork here. Need to check why below line will hung the executor
            //thread(&CephExecutor::startLongRunning,*this,"docker", dockerCommand).detach();
            startLongRunning("docker",dockerCommand);
        } else {
          bool started = block_until_started(containerName, "30");
          if (started) {
            LOG(INFO) << "Starting OSD task " << myTaskId.value();
            //send the OSD id back to let scheduler remove it
            //format: <MessageToScheduler::CONSUMED_OSD_ID>.OSDID
            string msg =
              lexical_cast<string>(static_cast<int>(MessageToScheduler::CONSUMED_OSD_ID)) +
                "." + tokens[1];
            status.set_message(msg);
            status.set_state(TASK_RUNNING);
          } else {
            LOG(INFO) << "Failed to start OSD task " << myTaskId.value();
            status.set_state(TASK_FAILED);
          }
          driver->sendStatusUpdate(status);

        }//end else "0==pid"
      } else {
        LOG(INFO) << "No OSD ID given!";
      }
      break;
    default:
      LOG(INFO) << "unknown message from scheduler";
  }

}

void CephExecutor::shutdown(ExecutorDriver* driver)
{
  if ("" != localMountOSDDir) {
    LOG(INFO) << "Umount( "<<localMountOSDDir<<" )";
    LOG(INFO) << runShellCommand("umount -f " + localMountOSDDir);
  }
  if ("" != localMountJNLDir) {
    LOG(INFO) << "Umount( "<<localMountJNLDir<<" )";
    LOG(INFO) << runShellCommand("umount -f " + localMountJNLDir);
  }
  LOG(INFO) << "Killing this container process";
  LOG(INFO) << runShellCommand("docker rm -f " + containerName);
  TaskStatus status;
  status.mutable_task_id()->MergeFrom(myTaskId);
  status.set_state(TASK_KILLED);
  driver->sendStatusUpdate(status);
}

string CephExecutor::runShellCommand(string cmd)
{
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return "Run shell command ERROR!";
  char buffer[128];
  std::string result = "";
  while (!feof(pipe)) {
    if (fgets(buffer, 128, pipe) != NULL) {
      result += buffer;
    }
  }
  size_t last = result.find_last_not_of('\n');
  result = result.substr(0,(last+1));
  pclose(pipe);
  return result;
}

void CephExecutor::registered(
      ExecutorDriver* driver,
      const ExecutorInfo& executorInfo,
      const FrameworkInfo& frameworkInfo,
      const SlaveInfo& slaveInfo)
{
  //set class member myHostname
  myHostname = slaveInfo.hostname();
  LOG(INFO) << "Registered executor on " << myHostname;
  //sandboxAbsolutePath
  char temp[4096];
  sandboxAbsolutePath = getcwd(temp, 4096) ? string(temp) : std::string("");
  LOG(INFO) << "sandbox absolute path: " << sandboxAbsolutePath;
}

//when the task before starting,
//it should check task.data() to determin
//what it will do, whether copy config?
//whether start fileserver?
//
//task.data() here format is :
//<isInitialMonNode>.<TaskType>
void CephExecutor::launchTask(ExecutorDriver* driver, const TaskInfo& task)
{
  //set class member localSharedConfDirRoot
  string cmd = "echo ~";
  string r = runShellCommand(cmd);
  remove_if(r.begin(), r.end(), isspace);
  localSharedConfigDirRoot = r == "" ? r :"/root";
  LOG(INFO) << "localSharedConfigDirRoot is " << localSharedConfigDirRoot;

  bool needCopyConfig = true;
  bool needStartFileServer = false;
  int taskType;
  if (task.has_data()) {
    LOG(INFO) << "Got TaskInfo data: " << task.data();
    vector<string> tokens = StringUtil::explode(task.data(),'.');
    //split by '.', the first part is isInitialMonNode,
    //second part is used for task type
    if (tokens[0] == "1"){
      needCopyConfig = false;
      deleteConfigDir(localConfigDirName);
    }
    taskType = lexical_cast<int>(tokens[1]);
  }

  string localMountDir = localSharedConfigDirRoot +
      "/" +localConfigDirName;
  TaskStatus status;
  status.mutable_task_id()->MergeFrom(task.task_id());

  //parse custom data from scheduler, include mgmtdev, datadev and so on
  if(!parseHostConfig(task)) {
    status.set_state(TASK_FAILED);
    driver->sendStatusUpdate(status);
    return;
  }

  //make local shared dir, all type of task need this:
  if (!createLocalSharedConfigDir(localConfigDirName)) {
    LOG(INFO) << "created local shared directory failed!";
    status.set_state(TASK_FAILED);
    driver->sendStatusUpdate(status);
    return;
  }
  LOG(INFO) << "Create directory tree done.";
  //copy shared config file
  if (needCopyConfig) {
    if (!copySharedConfigFiles()) {
      LOG(INFO) << "Copy shared config file failed!";
      status.set_state(TASK_FAILED);
      driver->sendStatusUpdate(status);
      return;
    }
    LOG(INFO) << "Copy config files done.";
  }

  //run docker command for MON and RADOSGW
  string cName = getContainerName(task.task_id().value());
  //set class member containerName, and myTaskId
  //TODO: see if put these in registed is more proper
  containerName = cName;
  myTaskId = task.task_id();
  //TODO: kill existing container in case conflict
  runShellCommand("docker rm -f " + containerName);

  string dockerCommand;
  switch (taskType) {
    case static_cast<int>(TaskType::MON):
      needStartFileServer = true;
      dockerCommand = constructMonCommand(
          localMountDir,
          cName);
      downloadDockerImage("ceph/daemon");
      break;
    case static_cast<int>(TaskType::OSD):
      downloadDockerImage("ceph/daemon");
      //Will get osdId in FrameworkMessage
      dockerCommand = "";
      status.set_state(TASK_STARTING);
      driver->sendStatusUpdate(status);
      return;
    case static_cast<int>(TaskType::RADOSGW):
      downloadDockerImage("ceph/daemon");
      dockerCommand = constructRADOSGWCommand(
          localMountDir,
          cName);
      break;
  }


  LOG(INFO) << "Stating container with command: ";
  LOG(INFO) << dockerCommand;

  //fork a thread to enable docker long running.
  //TODO: <thread> here seems not working, figure it out
  //to find a better way

  myPID = fork();
  if (0 == myPID){
    //child long running docker thread
    //TODO: we use fork here. Need to check why below line will hung the executor
    //thread(&CephExecutor::startLongRunning,*this,"docker", dockerCommand).detach();
    startLongRunning("docker",dockerCommand);
  } else {
    //parent thread
    //check if started normally
    bool started = block_until_started(cName, "60");
    if (started) {
      LOG(INFO) << "Starting task " << task.task_id().value();
      status.set_state(TASK_RUNNING);
      if (needStartFileServer) {
        //copy all bootstrap keyring to /etc/ceph
        string prefix = localSharedConfigDirRoot + "/" + localConfigDirName;
        //change the ceph.conf
        string c_cmd = "sed -i '/osd journal size/d' " + prefix +
            "/etc/ceph/ceph.conf; echo $?";
        r = runShellCommand(c_cmd);
        if ("0" != r) {
          LOG(INFO) << "change ceph.conf failed!";
          LOG(INFO) << c_cmd;
        }
        string osd_bootstrap_key = prefix + "/var/lib/ceph/bootstrap-osd/ceph.keyring";
        string tmp_osd_key = prefix + "/etc/ceph/ceph.keyring.osd";
        string rgw_bootstrap_key = prefix + "/var/lib/ceph/bootstrap-rgw/ceph.keyring";
        string tmp_rgw_key = prefix + "/etc/ceph/ceph.keyring.rgw";
        string mds_bootstrap_key = prefix + "/var/lib/ceph/bootstrap-mds/ceph.keyring";
        string tmp_mds_key = prefix + "/etc/ceph/ceph.keyring.mds";
        string cmd = "cp " + osd_bootstrap_key + " " + tmp_osd_key +
            ";cp " + rgw_bootstrap_key + " " + tmp_rgw_key +
            ";cp " + mds_bootstrap_key + " " + tmp_mds_key +
            ";echo $?";
        string r = runShellCommand(cmd);
        if ("0" != r) {
          LOG(INFO) << "Copy keyrings to fileserver root failed!";
        }
        thread fileServerThread(fileServer,
            7777,
            prefix + "/etc/ceph/");
        fileServerThread.detach();
        LOG(INFO) << "Mon fileserver started";
      }
    } else {
      LOG(INFO) << "Failed to start task " << task.task_id().value();
      status.set_state(TASK_FAILED);
    }
    driver->sendStatusUpdate(status);
  }
}

int main(int argc, const char* argv[])
{
  CephExecutor executor;
  MesosExecutorDriver driver(&executor);
  return driver.run() == DRIVER_STOPPED ? 0 : 1;
}
