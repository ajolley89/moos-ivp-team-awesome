/*****************************************************************/
/*    NAME: Michael Benjamin // Jason Barker                     */
/*    ORGN: Dept of Mechanical Eng / CSAIL, MIT Cambridge MA     */
/*    FILE: HazardMgr.cpp                                        */
/*    DATE: Oct 26th 2012   // Apr 04 2019                       */
/*                                                               */
/* This file is part of MOOS-IvP                                 */
/*                                                               */
/* MOOS-IvP is free software: you can redistribute it and/or     */
/* modify it under the terms of the GNU General Public License   */
/* as published by the Free Software Foundation, either version  */
/* 3 of the License, or (at your option) any later version.      */
/*                                                               */
/* MOOS-IvP is distributed in the hope that it will be useful,   */
/* but WITHOUT ANY WARRANTY; without even the implied warranty   */
/* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See  */
/* the GNU General Public License for more details.              */
/*                                                               */
/* You should have received a copy of the GNU General Public     */
/* License along with MOOS-IvP.  If not, see                     */
/* <http://www.gnu.org/licenses/>.                               */
/*****************************************************************/

#include <iterator>
#include "MBUtils.h"
#include "HazardMgr.h"
#include "XYFormatUtilsHazard.h"
#include "XYFormatUtilsPoly.h"
#include "XYFormatUtilsHazardSet.h"
#include "ACTable.h"
#include "NodeMessage.h"

using namespace std;

//---------------------------------------------------------
// Constructor

HazardMgr::HazardMgr()
{
  // Config variables
  m_swath_width_desired = 25;
  m_pd_desired          = 0.9;

  // State Variables 
  m_sensor_config_requested = false;
  m_sensor_config_set   = false;
  m_swath_width_granted = 0;
  m_pd_granted          = 0;

  m_sensor_config_reqs = 0;
  m_sensor_config_acks = 0;
  m_sensor_report_reqs = 0;
  m_detection_reports  = 0;

  m_summary_reports = 0;
  m_sent_timer = MOOSTime();
}

//---------------------------------------------------------
// Procedure: OnNewMail

bool HazardMgr::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key   = msg.GetKey();
    string sval  = msg.GetString(); 

#if 0 // Keep these around just for template
    string comm  = msg.GetCommunity();
    double dval  = msg.GetDouble();
    string msrc  = msg.GetSource();
    double mtime = msg.GetTime();
    bool   mdbl  = msg.IsDouble();
    bool   mstr  = msg.IsString();
#endif
    
    if(key == "UHZ_CONFIG_ACK") 
      handleMailSensorConfigAck(sval);

    else if(key == "UHZ_OPTIONS_SUMMARY") 
      handleMailSensorOptionsSummary(sval);

    else if(key == "UHZ_DETECTION_REPORT"){ 
      handleMailDetectionReport(sval);
      m_waypoints.add_vertex(m_dbl_x, m_dbl_y);
    }

    else if(key == "HAZARDSET_REQUEST") 
      handleMailReportRequest();
 
    else if(key == "HAZARDSET_REPORT")
      ;

    else if(key == "UHZ_MISSION_PARAMS") 
      handleMailMissionParams(sval);

    else if(key == "NAV_X"){
        m_current_x = msg.GetDouble();
      }
    else if(key == "NAV_Y"){
        m_current_y = msg.GetDouble();
      }
    
    else if(key == "NODE_REPORT"){
      m_timer = MOOSTime();
    }

    else if(key == "UHZ_HAZARD_REPORT") {  // KJ KJ KJ KJ KJ KJ KJ 
       reportEvent("UHZ_HAZARD_REPORT Recieved");
       handleMailHazardReport(sval);
    }

    else if(key == "GENPATH_REGENERATE"){
      //m_waypoints.add_vertex(m_current_x, m_current_y);
      XYSegList sorted_waypoints;
      XYSegList working_waypoints = m_waypoints;
      int closest_index = working_waypoints.closest_vertex(m_current_x, m_current_y); 
      double next_x = working_waypoints.get_vx(closest_index);
      double next_y = working_waypoints.get_vy(closest_index);
      working_waypoints.delete_vertex(closest_index);
      sorted_waypoints.add_vertex(next_x, next_y); 
      for(int i=1; i<m_waypoints.size(); i++){
      int closest_index = working_waypoints.closest_vertex(next_x, next_y);
       next_x = working_waypoints.get_vx(closest_index);
      next_y = working_waypoints.get_vy(closest_index);
      sorted_waypoints.add_vertex(next_x, next_y);
      working_waypoints.delete_vertex(closest_index);  
      }
      string color;
        if(next_x > 88){
        color = "red";
        }
        else{
        color = "yellow";
     }
        string update_str = "points = ";
        update_str += m_waypoints.get_spec();
        update_str += " # visual_hints = edge_color = " + color + ", vertex_color = " + color;
        Notify("WAYPOINT_UPDATE_" + m_report_name, update_str); 
    }

    else if(key == "HAZARDSET_OTHER"){
      handleMailConcatenateHazards(sval);
    }

    else {reportRunWarning("Unhandled Mail: " + key);};

  }
  
   return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer

bool HazardMgr::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool HazardMgr::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(!m_sensor_config_requested)
    postSensorConfigRequest();

  if(m_sensor_config_set)
    postSensorInfoRequest();



  //if(MOOSTime() - m_timer < 5 && MOOSTime() - m_sent_timer > 61){
     NodeMessage node_message;
     node_message.setSourceNode(m_report_name);
     node_message.setDestNode("all");
     node_message.setVarName("HAZARDSET_OTHER");
     m_hazard_queue.setSource(m_report_name);
     m_hazardset_local = m_hazard_queue.getSpec();
     node_message.setStringVal(m_hazardset_local);
     string msg = node_message.getSpec();
     Notify("NODE_MESSAGE_LOCAL", msg);
     m_hazard_queue.clear();
     m_sent_timer = MOOSTime();
  //}
  
    XYHazardSet scrubbing_hazardset;
  for(int haznum = 0; haznum < m_hazard_set.size(); haznum ++) {
    XYHazard hazard_fun = m_hazard_set.getHazard(haznum);
    if(hazard_fun.getType() == "hazard"){
      scrubbing_hazardset.addHazard(hazard_fun);
    }
  }
  m_hazard_set = scrubbing_hazardset;
  m_hazard_set.setSource(m_report_name);

    int closest_waypoint = m_waypoints.closest_vertex(m_current_x, m_current_y); //find the vertext closest to the current position
    double closest_x = m_waypoints.get_vx(closest_waypoint);
    double closest_y = m_waypoints.get_vy(closest_waypoint);
    double distance = sqrt(pow((closest_x-m_current_x),2)+pow((closest_y-m_current_y),2));
    if(distance < 10){
      m_waypoints.delete_vertex(closest_waypoint);}

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool HazardMgr::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(true);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if((param == "swath_width") && isNumber(value)) {
      m_swath_width_desired = atof(value.c_str());
      handled = true;
    }
    else if(((param == "sensor_pd") || (param == "pd")) && isNumber(value)) {
      m_pd_desired = atof(value.c_str());
      handled = true;
    }
    else if(param == "report_name") {
      value = stripQuotes(value);
      m_report_name = value;
      handled = true;
    }
    else if(param == "other_vehicle") {
      value = stripQuotes(value);
      m_other_vehicle = toupper(value);
      handled = true;
    }


    else if(param == "region") {
      XYPolygon poly = string2Poly(value);
      if(poly.is_convex())
       m_search_region = poly;
      handled = true;
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);
  }
  
  m_hazard_set.setSource(m_host_community);
  m_hazard_set.setName(m_report_name);
  m_hazard_set.setRegion(m_search_region);
  
  registerVariables();  
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables

void HazardMgr::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("UHZ_DETECTION_REPORT", 0);
  Register("UHZ_HAZARD_REPORT", 0); // KJ KJ KJ KJ KJ KJ KJ
  Register("UHZ_CONFIG_ACK", 0);
  Register("UHZ_OPTIONS_SUMMARY", 0);
  Register("UHZ_MISSION_PARAMS", 0);
  Register("HAZARDSET_REQUEST", 0);
 // Register("NODE_MESSAGE",0);
 // Register("NODE_MESSAGE_LOCAL",0);
  Register("HAZARDSET_REPORT",0);
  Register("GENPATH_REGENERATE", 0);
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
  Register("HAZARDSET_OTHER");
  Register("NODE_REPORT");
}

//---------------------------------------------------------
// Procedure: postSensorConfigRequest

void HazardMgr::postSensorConfigRequest()
{
  string request = "vname=" + m_host_community;

  //  if(m_report_name=="jake")
  //   {
  //     // default: exp=2 and width=50 and Pd=60
  //     double penalty_ratio = m_penalty_mh / m_penalty_fa;
  //     if(penalty_ratio > 3) {
  //     m_swath_width_desired=50;
  //     m_pd_desired=0.7;
  // //go after both the hazards and the benigns because pFA =1 and 50/50 chance
  //     }
  //     else if(penalty_ratio < 3) {
  // m_swath_width_desired=25;
  // m_pd_desired=0.7;
  // //go after the hazards only, not the benigns because Jake's sensor is pretty good at pFA = 0.3
  //     }
  //   } 
  request += ",width=" + doubleToStringX(m_swath_width_desired,2);
  request += ",pd="    + doubleToStringX(m_pd_desired,2);

  m_sensor_config_requested = true;
  m_sensor_config_reqs++;
  Notify("UHZ_CONFIG_REQUEST", request);
}

//---------------------------------------------------------
// Procedure: postSensorInfoRequest

void HazardMgr::postSensorInfoRequest()
{
  string request = "vname=" + m_host_community;

  m_sensor_report_reqs++;
  Notify("UHZ_SENSOR_REQUEST", request);
}

//---------------------------------------------------------
// Procedure: handleMailSensorConfigAck

bool HazardMgr::handleMailSensorConfigAck(string str)
{
  // Expected ack parameters:
  string vname, width, pd, pfa, pclass;
  
  // Parse and handle ack message components
  bool   valid_msg = true;
  string original_msg = str;

  vector<string> svector = parseString(str, ',');
  unsigned int i, vsize = svector.size();
  for(i=0; i<vsize; i++) {
    string param = biteStringX(svector[i], '=');
    string value = svector[i];

    if(param == "vname")
      vname = value;
    else if(param == "pd")
      pd = value;
    else if(param == "width")
      width = value;
    else if(param == "pfa")
      pfa = value;
    else if(param == "pclass")
      pclass = value;
    else
      valid_msg = false;       

  }


  if((vname=="")||(width=="")||(pd=="")||(pfa=="")||(pclass==""))
    valid_msg = false;
  
  if(!valid_msg)
    reportRunWarning("Unhandled Sensor Config Ack:" + original_msg);

  
  if(valid_msg) {
    m_sensor_config_set = true;
    m_sensor_config_acks++;
    m_swath_width_granted = atof(width.c_str());
    m_pd_granted = atof(pd.c_str());
  }

  return(valid_msg);
}
//------------------------------------------------------------
// Procedure: handleMailConcatenateHazards

void HazardMgr::handleMailConcatenateHazards(string str)
{
  XYHazardSet m_hazard_incoming = string2HazardSet(str);
  for(unsigned int i=0; i<m_hazard_incoming.getHazardCnt(); i++) {
    XYHazard my_hazard = m_hazard_incoming.getHazard(i);
    if(!m_hazard_set.hasHazard(my_hazard.getLabel())) {
      m_hazard_set.addHazard(my_hazard);
      m_waypoints.add_vertex(my_hazard.getX(), my_hazard.getY());

       string event = "Remote Detection, label=" + my_hazard.getLabel();
       event += ", x=" + doubleToString(my_hazard.getX(),1);
        event += ", y=" + doubleToString(my_hazard.getY(),1);
        reportEvent(event);
if(m_report_name == "kasper") Notify("GENPATH_REGENERATE", "true");

//        string req = "vname=" + m_host_community + ",label=" + my_hazard.getLabel();
//        Notify("UHZ_CLASSIFY_REQUEST", req);

      }

  }
}

//---------------------------------------------------------
// Procedure: handleMailDetectionReport
//      Note: The detection report should look something like:
//            UHZ_DETECTION_REPORT = vname=betty,x=51,y=11.3,label=12 

bool HazardMgr::handleMailDetectionReport(string str)
{
  m_detection_reports++;

  XYHazard new_hazard = string2Hazard(str);
  new_hazard.setType("hazard");

  string hazlabel = new_hazard.getLabel();
  
  if(hazlabel == "") {
    reportRunWarning("Detection report received for hazard w/out label");
    return(false);
  }

  int ix = m_hazard_set.findHazard(hazlabel);
  if(ix == -1){
    m_hazard_set.addHazard(new_hazard);
    m_hazard_queue.addHazard(new_hazard);}
  // else {     // KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ 
  // //  XYHazard compareHazard = m_hazard_set.getHazard(ix);
  // //  if((m_report_name=="jake") || (compareHazard.gxetSource()=="jake"))
  // //    { //if I'm Jake, then overwrite. If what I have is from Jake, then overwrite. 
  // //m_hazard_set.setHazard(ix, new_hazard);  // KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ KJ 
  //   //  }
  // }

  string event = "New Detection, label=" + new_hazard.getLabel();
  event += ", x=" + doubleToString(new_hazard.getX(),1);
  event += ", y=" + doubleToString(new_hazard.getY(),1);
  
  m_dbl_x = new_hazard.getX();
  m_dbl_y = new_hazard.getY();

  reportEvent(event);

  string req = "vname=" + m_host_community + ",label=" + hazlabel;
if(m_report_name == "kasper") {
  Notify("UHZ_CLASSIFY_REQUEST", req);
  reportEvent("UHZ_CLASSIFY_REQUEST Sent");
}
  return(true);
}


//---------------------------------------------------------
// Procedure: handleMailReportRequest

void HazardMgr::handleMailReportRequest()
{
  m_summary_reports++;

  m_hazard_set.findMinXPath(20);
  //unsigned int count    = m_hazard_set.findMinXPath(20);
  m_update_hazard_set.findMinXPath(20);
  m_update_hazard_set.setSource(m_report_name);
  string summary_report = m_update_hazard_set.getSpec();
  
  Notify("HAZARDSET_REPORT", summary_report);
}


//---------------------------------------------------------
// Procedure: handleMailMissionParams
//   Example: UHZ_MISSION_PARAMS = penalty_missed_hazard=100,               
//                       penalty_nonopt_hazard=55,                
//                       penalty_false_alarm=35,                  
//                       penalty_max_time_over=200,               
//                       penalty_max_time_rate=0.45,              
//                       transit_path_width=25,                           
//                       search_region = pts={-150,-75:-150,-50:40,-50:40,-75}


void HazardMgr::handleMailMissionParams(string str)
{
  vector<string> svector = parseStringZ(str, ',', "{");
  unsigned int i, vsize = svector.size();
  for(i=0; i<vsize; i++) {
    string param = biteStringX(svector[i], '=');
    string value = svector[i];
    if(param=="penalty_false_alarm") {m_penalty_fa = stoi(value);}
    else if(param=="penalty_missed_hazard") {m_penalty_mh = stoi(value);}
    else if(param=="max_time") {m_max_time = stoi(value);}
    // This needs to be handled by the developer. Just a placeholder.
  }
  return;
}



//------------------------------------------------------------
// Procedure: buildReport()

bool HazardMgr::buildReport() 
{
  m_msgs << "Config Requested:"                                  << endl;
  m_msgs << "    swath_width_desired: " << m_swath_width_desired << endl;
  m_msgs << "             pd_desired: " << m_pd_desired          << endl;
  m_msgs << "   config requests sent: " << m_sensor_config_reqs  << endl;
  m_msgs << "                  acked: " << m_sensor_config_acks  << endl;
  m_msgs << "------------------------ "                          << endl;
  m_msgs << "Config Result:"                                     << endl;
  m_msgs << "       config confirmed: " << boolToString(m_sensor_config_set) << endl;
  m_msgs << "    swath_width_granted: " << m_swath_width_granted << endl;
  m_msgs << "             pd_granted: " << m_pd_granted          << endl << endl;
  m_msgs << "--------------------------------------------" << endl << endl;

  m_msgs << "               sensor requests: " << m_sensor_report_reqs << endl;
  m_msgs << "             detection reports: " << m_detection_reports  << endl << endl; 

  m_msgs << "   Hazardset Reports Requested: " << m_summary_reports << endl;
  m_msgs << "      Hazardset Reports Posted: " << m_summary_reports << endl;
  m_msgs << "                   Report Name: " << m_report_name << endl;

  return(true);
}
//------------------------------------------------------------
bool HazardMgr::handleMailHazardReport(string str) //  KJ KJ KJ KJ KJ KJ KJ 
{
  XYHazard classifiedHazard = string2Hazard(str);
  classifiedHazard.setSource(m_report_name);
  int index = -1;
  if(classifiedHazard.getType()=="hazard") m_update_hazard_set.addHazard(classifiedHazard);

  index = m_hazard_set.findHazard(classifiedHazard.getLabel());
  if(index==-1) { // if !exist in m_hazard_set, add hazard as Jake-sourced since unknown source
    classifiedHazard.setSource("jake");
    m_hazard_set.addHazard(classifiedHazard);
  }
  else // it does already exist in the m_hazard_set
    {
      XYHazard currentHazard=m_hazard_set.getHazard(index);
      string tp=currentHazard.getType();
      if((tp=="benign")) {
  //if the type is currently empty or the source is currently jake
  currentHazard.setType(classifiedHazard.getType()); // set type
  m_hazard_set.setHazard(index, currentHazard); // overwrite that hazard to m_hazard_set
  reportEvent("Benign detection");
      }
    }
      //we assume any classification done once by kasper is final, so no need to overwrite
      //...kasper on kasper
  
  return true;
}


