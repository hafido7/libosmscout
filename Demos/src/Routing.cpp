/*
  Routing - a demo program for libosmscout
  Copyright (C) 2009  Tim Teulings

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <list>

#include <osmscout/Database.h>
#include <osmscout/Router.h>
#include <osmscout/RoutePostprocessor.h>

#include <osmscout/util/Geometry.h>

//#define POINTS_DEBUG
//#define ROUTE_DEBUG
//#define NODE_DEBUG

/*
  Examples for the nordrhein-westfalen.osm:

  Long: "In den Hüchten" Dortmund => Promenadenweg Bonn
    51.5717798, 7.4587852  50.6890143, 7.1360549

  Medium: "In den Hüchten" Dortmund => "Zur Taubeneiche" Arnsberg
     51.5717798, 7.4587852  51.3846946, 8.0771719

  Short: "In den Hüchten" Dortmund => "An der Dorndelle" Bergkamen
     51.5717798, 7.4587852  51.6217831, 7.6026704

  Roundabout: "Am Hohen Kamp" Bergkamen => Opferweg Bergkamen
     51.6163438, 7.5952355  51.6237998, 7.6419474

  Oneway Routing: Viktoriastraße Dortmund => Schwanenwall Dortmund
     51.5130296, 7.4681888  51.5146904, 7.4725241
*/

static std::string TimeToString(double time)
{
  std::ostringstream stream;

  stream << std::setfill(' ') << std::setw(2) << (int)std::floor(time) << ":";

  time-=std::floor(time);

  stream << std::setfill('0') << std::setw(2) << (int)floor(60*time+0.5);

  return stream.str();
}

static void GetCarSpeedTable(std::map<std::string,double>& map)
{
  map["highway_motorway"]=110.0;
  map["highway_motorway_trunk"]=100.0;
  map["highway_motorway_primary"]=70.0;
  map["highway_motorway_link"]=60.0;
  map["highway_motorway_junction"]=60.0;
  map["highway_trunk"]=100.0;
  map["highway_trunk_link"]=60.0;
  map["highway_primary"]=70.0;
  map["highway_primary_link"]=60.0;
  map["highway_secondary"]=60.0;
  map["highway_secondary_link"]=50.0;
  map["highway_tertiary"]=55.0;
  map["highway_unclassified"]=50.0;
  map["highway_road"]=50.0;
  map["highway_residential"]=40.0;
  map["highway_roundabout"]=40.0;
  map["highway_living_street"]=10.0;
  map["highway_service"]=30.0;
}

static std::string MoveToTurnCommand(osmscout::RouteDescription::DirectionDescription::Move move)
{
  switch (move) {
  case osmscout::RouteDescription::DirectionDescription::sharpLeft:
    return "Turn sharp left";
  case osmscout::RouteDescription::DirectionDescription::left:
    return "Turn left";
  case osmscout::RouteDescription::DirectionDescription::slightlyLeft:
    return "Turn slightly left";
  case osmscout::RouteDescription::DirectionDescription::straightOn:
    return "Straight on";
  case osmscout::RouteDescription::DirectionDescription::slightlyRight:
    return "Turn slightly right";
  case osmscout::RouteDescription::DirectionDescription::right:
    return "Turn right";
  case osmscout::RouteDescription::DirectionDescription::sharpRight:
    return "Turn sharp right";
  }

  assert(false);

  return "???";
}

static std::string CrossingWaysDescriptionToString(const osmscout::RouteDescription::CrossingWaysDescription& crossingWaysDescription)
{
  std::set<std::string>                          names;
  osmscout::RouteDescription::NameDescriptionRef originDescription=crossingWaysDescription.GetOriginDesccription();
  osmscout::RouteDescription::NameDescriptionRef targetDescription=crossingWaysDescription.GetTargetDesccription();

  if (originDescription.Valid()) {
    std::string nameString=originDescription->GetDescription();

    if (!nameString.empty()) {
      names.insert(nameString);
    }
  }

  if (targetDescription.Valid()) {
    std::string nameString=targetDescription->GetDescription();

    if (!nameString.empty()) {
      names.insert(nameString);
    }
  }

  for (std::list<osmscout::RouteDescription::NameDescriptionRef>::const_iterator name=crossingWaysDescription.GetDescriptions().begin();
      name!=crossingWaysDescription.GetDescriptions().end();
      ++name) {
    std::string nameString=(*name)->GetDescription();

    if (!nameString.empty()) {
      names.insert(nameString);
    }
  }

  if (names.size()>0) {
    std::ostringstream stream;

    for (std::set<std::string>::const_iterator name=names.begin();
        name!=names.end();
        ++name) {
      if (name!=names.begin()) {
        stream << ", ";
      }
      stream << "'" << *name << "'";
    }

    return stream.str();
  }
  else {
    return "";
  }
}

static bool HasRelevantDescriptions(const osmscout::RouteDescription::Node& node)
{
#if defined(ROUTE_DEBUG)
  return true;
#else
  if (node.HasDescription(osmscout::RouteDescription::NODE_START_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::NODE_TARGET_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::WAY_NAME_CHANGED_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::ROUNDABOUT_ENTER_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::ROUNDABOUT_LEAVE_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::TURN_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::MOTORWAY_ENTER_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::MOTORWAY_CHANGE_DESC)) {
    return true;
  }

  if (node.HasDescription(osmscout::RouteDescription::MOTORWAY_LEAVE_DESC)) {
    return true;
  }

  return false;
#endif
}

static void NextLine(size_t& lineCount)
{
  if (lineCount>0) {
    std::cout << "                             ";
  }

  lineCount++;
}

static void DumpStartDescription(size_t& lineCount,
                                 const osmscout::RouteDescription::StartDescriptionRef& startDescription,
                                 const osmscout::RouteDescription::NameDescriptionRef& nameDescription)
{
  NextLine(lineCount);
  std::cout << "Start at '" << startDescription->GetDescription() << "'" << std::endl;

  if (nameDescription.Valid() &&
      nameDescription->HasName()) {
    NextLine(lineCount);
    std::cout << "Drive along '" << nameDescription->GetDescription() << "'" << std::endl;
  }
}

static void DumpTargetDescription(size_t& lineCount,
                                  const osmscout::RouteDescription::TargetDescriptionRef& targetDescription)
{
  NextLine(lineCount);

  std::cout << "Target reached '" << targetDescription->GetDescription() << "'" << std::endl;
}

static void DumpTurnDescription(size_t& lineCount,
                                const osmscout::RouteDescription::TurnDescriptionRef& turnDescription,
                                const osmscout::RouteDescription::CrossingWaysDescriptionRef& crossingWaysDescription,
                                const osmscout::RouteDescription::DirectionDescriptionRef& directionDescription,
                                const osmscout::RouteDescription::NameDescriptionRef& nameDescription)
{
  std::string crossingWaysString;

  if (crossingWaysDescription.Valid()) {
    crossingWaysString=CrossingWaysDescriptionToString(crossingWaysDescription);
  }

  if (!crossingWaysString.empty()) {
    NextLine(lineCount);
    std::cout << "At crossing " << crossingWaysString << std::endl;
  }

  NextLine(lineCount);
  if (directionDescription.Valid()) {
    std::cout << MoveToTurnCommand(directionDescription->GetCurve());
  }
  else {
    std::cout << "Turn";
  }

  if (nameDescription.Valid() &&
      nameDescription->HasName()) {
    std::cout << " into '" << nameDescription->GetDescription() << "'";
  }

  std::cout << std::endl;
}

static void DumpRoundaboutEnterDescription(size_t& lineCount,
                                           const osmscout::RouteDescription::RoundaboutEnterDescriptionRef& roundaboutEnterDescription,
                                           const osmscout::RouteDescription::CrossingWaysDescriptionRef& crossingWaysDescription)
{
  std::string crossingWaysString;

  if (crossingWaysDescription.Valid()) {
    crossingWaysString=CrossingWaysDescriptionToString(crossingWaysDescription);
  }

  if (!crossingWaysString.empty()) {
    NextLine(lineCount);
    std::cout << "At crossing " << crossingWaysString << std::endl;
  }

  NextLine(lineCount);
  std::cout << "Enter roundabout" << std::endl;
}

static void DumpRoundaboutLeaveDescription(size_t& lineCount,
                                           const osmscout::RouteDescription::RoundaboutLeaveDescriptionRef& roundaboutLeaveDescription,
                                           const osmscout::RouteDescription::NameDescriptionRef& nameDescription)
{
  NextLine(lineCount);
  std::cout << "Leave roundabout (" << roundaboutLeaveDescription->GetExitCount() << ". exit)";

  if (nameDescription.Valid() &&
      nameDescription->HasName()) {
    std::cout << " into street '" << nameDescription->GetDescription() << "'";
  }

  std::cout << std::endl;
}

static void DumpMotorwayEnterDescription(size_t& lineCount,
                                         const osmscout::RouteDescription::MotorwayEnterDescriptionRef& motorwayEnterDescription,
                                         const osmscout::RouteDescription::CrossingWaysDescriptionRef& crossingWaysDescription)
{
  std::string crossingWaysString;

  if (crossingWaysDescription.Valid()) {
    crossingWaysString=CrossingWaysDescriptionToString(crossingWaysDescription);
  }

  if (!crossingWaysString.empty()) {
    NextLine(lineCount);
    std::cout << "At crossing " << crossingWaysString << std::endl;
  }

  NextLine(lineCount);
  std::cout << "Enter motorway";

  if (motorwayEnterDescription->GetToDescription().Valid() &&
      motorwayEnterDescription->GetToDescription()->HasName()) {
    std::cout << " '" << motorwayEnterDescription->GetToDescription()->GetDescription() << "'";
  }

  std::cout << std::endl;
}

static void DumpMotorwayChangeDescription(size_t& lineCount,
                                          const osmscout::RouteDescription::MotorwayChangeDescriptionRef& motorwayChangeDescription)
{
  NextLine(lineCount);
  std::cout << "Change motorway";

  if (motorwayChangeDescription->GetFromDescription().Valid() &&
      motorwayChangeDescription->GetFromDescription()->HasName()) {
    std::cout << " from '" << motorwayChangeDescription->GetFromDescription()->GetDescription() << "'";
  }

  if (motorwayChangeDescription->GetToDescription().Valid() &&
      motorwayChangeDescription->GetToDescription()->HasName()) {
    std::cout << " to '" << motorwayChangeDescription->GetToDescription()->GetDescription() << "'";
  }

  std::cout << std::endl;
}

static void DumpMotorwayLeaveDescription(size_t& lineCount,
                                         const osmscout::RouteDescription::MotorwayLeaveDescriptionRef& motorwayLeaveDescription,
                                         const osmscout::RouteDescription::DirectionDescriptionRef& directionDescription,
                                         const osmscout::RouteDescription::NameDescriptionRef& nameDescription)
{
  NextLine(lineCount);
  std::cout << "Leave motorway";

  if (motorwayLeaveDescription->GetFromDescription().Valid() &&
      motorwayLeaveDescription->GetFromDescription()->HasName()) {
    std::cout << " '" << motorwayLeaveDescription->GetFromDescription()->GetDescription() << "'";
  }

  if (directionDescription.Valid() &&
      directionDescription->GetCurve()!=osmscout::RouteDescription::DirectionDescription::slightlyLeft &&
      directionDescription->GetCurve()!=osmscout::RouteDescription::DirectionDescription::straightOn &&
      directionDescription->GetCurve()!=osmscout::RouteDescription::DirectionDescription::slightlyRight) {
    std::cout << " " << MoveToTurnCommand(directionDescription->GetCurve());
  }

  if (nameDescription.Valid() &&
      nameDescription->HasName()) {
    std::cout << " into '" << nameDescription->GetDescription() << "'";
  }

  std::cout << std::endl;
}

static void DumpNameChangedDescription(size_t& lineCount,
                                       const osmscout::RouteDescription::NameChangedDescriptionRef& nameChangedDescription)
{
  NextLine(lineCount);

  std::cout << "Way changes name";
  if (nameChangedDescription->GetOriginDesccription().Valid()) {
    std::cout << " from ";
    std::cout << "'" << nameChangedDescription->GetOriginDesccription()->GetDescription() << "'";
  }

  std::cout << " to '";
  std::cout << nameChangedDescription->GetTargetDesccription()->GetDescription();
  std::cout << "'" << std::endl;
}

int main(int argc, char* argv[])
{
  osmscout::Vehicle                         vehicle=osmscout::vehicleCar;
  osmscout::FastestPathRoutingProfile       routingProfile;
  std::string                               map;

  double                                    startLat;
  double                                    startLon;

  double                                    targetLat;
  double                                    targetLon;

  osmscout::ObjectFileRef                   startObject;
  size_t                                    startNodeIndex;

  osmscout::ObjectFileRef                   targetObject;
  size_t                                    targetNodeIndex;

  bool                                      outputGPX = false;

  int currentArg=1;
  while (currentArg<argc) {
    if (strcmp(argv[currentArg],"--foot")==0) {
      vehicle=osmscout::vehicleFoot;

      currentArg++;
    }
    else if (strcmp(argv[currentArg],"--bicycle")==0) {
      vehicle=osmscout::vehicleBicycle;
      currentArg++;
    }
    else if (strcmp(argv[currentArg],"--car")==0) {
      vehicle=osmscout::vehicleCar;
      currentArg++;
    }
    else if (strcmp(argv[currentArg],"--gpx")==0) {
      outputGPX=true;
      currentArg++;
    }
    else {
      // No more "special" arguments
      break;
    }
  }

  if (argc-currentArg!=5) {
    std::cout << "Routing <map directory>" <<std::endl;
    std::cout << "        <start lat> <start lon>" << std::endl;
    std::cout << "        <target lat> <target lon>" << std::endl;
    return 1;
  }

  map=argv[currentArg];
  currentArg++;

  if (sscanf(argv[currentArg],"%lf",&startLat)!=1) {
    std::cerr << "lat is not numeric!" << std::endl;
    return 1;
  }
  currentArg++;

  if (sscanf(argv[currentArg],"%lf",&startLon)!=1) {
    std::cerr << "lon is not numeric!" << std::endl;
    return 1;
  }
  currentArg++;

  if (sscanf(argv[currentArg],"%lf",&targetLat)!=1) {
    std::cerr << "lat is not numeric!" << std::endl;
    return 1;
  }
  currentArg++;

  if (sscanf(argv[currentArg],"%lf",&targetLon)!=1) {
    std::cerr << "lon is not numeric!" << std::endl;
    return 1;
  }
  currentArg++;

  double tlat;
  double tlon;

  osmscout::GetEllipsoidalDistance(startLat,
                                   startLon,
                                   45,
                                   1000,
                                   tlat,
                                   tlon);

  if (!outputGPX) {
    std::cout << "[" << startLat << "," << startLon << "] => [" << tlat << "," << tlon << "]" << std::endl;
  }

  osmscout::DatabaseParameter databaseParameter;
  osmscout::Database          database(databaseParameter);

  if (!database.Open(map.c_str())) {
    std::cerr << "Cannot open database" << std::endl;

    return 1;
  }

  osmscout::RouterParameter routerParameter;

  if (!outputGPX) {
    routerParameter.SetDebugPerformance(true);
  }

  osmscout::Router          router(routerParameter,
                                   vehicle);

  if (!router.Open(map.c_str())) {
    std::cerr << "Cannot open routing database" << std::endl;

    return 1;
  }

  osmscout::TypeConfig                *typeConfig=router.GetTypeConfig();

  //osmscout::ShortestPathRoutingProfile routingProfile;
  osmscout::RouteData                 data;
  osmscout::RouteDescription          description;
  std::map<std::string,double>        carSpeedTable;

  switch (vehicle) {
  case osmscout::vehicleFoot:
    routingProfile.ParametrizeForFoot(*typeConfig,
                                      5.0);
    break;
  case osmscout::vehicleBicycle:
    routingProfile.ParametrizeForBicycle(*typeConfig,
                                         20.0);
    break;
  case osmscout::vehicleCar:
    GetCarSpeedTable(carSpeedTable);
    routingProfile.ParametrizeForCar(*typeConfig,
                                     carSpeedTable,
                                     160.0);
    break;
  }

  if (!outputGPX) {
    std::cout << "Searching for routing node for start location..." << std::endl;
  }

  if (!database.GetClosestRoutableNode(startLat,
                                       startLon,
                                       vehicle,
                                       1000,
                                       startObject,
                                       startNodeIndex)) {
    std::cerr << "Error while searching for routing node near start location!" << std::endl;
    return 1;
  }

  if (startObject.Invalid() || startObject.GetType()==osmscout::refNode) {
    std::cerr << "Cannot find start node for start location!" << std::endl;
  }

  if (!outputGPX) {
    std::cout << "Searching for routing node for target location..." << std::endl;
  }

  if (!database.GetClosestRoutableNode(targetLat,
                                       targetLon,
                                       vehicle,
                                       1000,
                                       targetObject,
                                       targetNodeIndex)) {
    std::cerr << "Error while searching for routing node near target location!" << std::endl;
    return 1;
  }

  if (targetObject.Invalid() || targetObject.GetType()==osmscout::refNode) {
    std::cerr << "Cannot find start node for target location!" << std::endl;
  }

  if (!router.CalculateRoute(routingProfile,
                             startObject,
                             startNodeIndex,
                             targetObject,
                             targetNodeIndex,
                             data)) {
    std::cerr << "There was an error while calculating the route!" << std::endl;
    router.Close();
    return 1;
  }

  if (data.IsEmpty()) {
    std::cout << "No Route found!" << std::endl;

    router.Close();

    return 0;
  }

  router.TransformRouteDataToRouteDescription(data,description);

  std::list<osmscout::RoutePostprocessor::PostprocessorRef> postprocessors;

  postprocessors.push_back(new osmscout::RoutePostprocessor::DistanceAndTimePostprocessor());
  postprocessors.push_back(new osmscout::RoutePostprocessor::StartPostprocessor("Start"));
  postprocessors.push_back(new osmscout::RoutePostprocessor::TargetPostprocessor("Target"));
  postprocessors.push_back(new osmscout::RoutePostprocessor::WayNamePostprocessor());
  postprocessors.push_back(new osmscout::RoutePostprocessor::CrossingWaysPostprocessor());
  postprocessors.push_back(new osmscout::RoutePostprocessor::DirectionPostprocessor());

  osmscout::RoutePostprocessor::InstructionPostprocessor *instructionProcessor=new osmscout::RoutePostprocessor::InstructionPostprocessor();

  instructionProcessor->AddMotorwayType(typeConfig->GetWayTypeId("highway_motorway"));
  instructionProcessor->AddMotorwayLinkType(typeConfig->GetWayTypeId("highway_motorway_link"));
  instructionProcessor->AddMotorwayType(typeConfig->GetWayTypeId("highway_motorway_trunk"));
  instructionProcessor->AddMotorwayType(typeConfig->GetWayTypeId("highway_motorway_primary"));
  instructionProcessor->AddMotorwayType(typeConfig->GetWayTypeId("highway_trunk"));
  instructionProcessor->AddMotorwayLinkType(typeConfig->GetWayTypeId("highway_trunk_link"));
  postprocessors.push_back(instructionProcessor);

  osmscout::RoutePostprocessor postprocessor;
  size_t                       roundaboutCrossingCounter=0;

#if defined(POINTS_DEBUG)
  std::list<osmscout::Point> points;

  if (!router.TransformRouteDataToPoints(data,points)) {
    std::cerr << "Error during route conversion" << std::endl;
  }

  std::cout << points.size() << " point(s)" << std::endl;
  for (std::list<osmscout::Point>::const_iterator point=points.begin();
      point!=points.end();
      ++point) {
    std::cout << "Point " << point->GetId() << " " << point->GetLat() << "," << point->GetLon() << std::endl;
  }
#endif

  std::list<osmscout::Point> points;

  if(outputGPX) {
    if (!router.TransformRouteDataToPoints(data,points)) {
      std::cerr << "Error during route conversion" << std::endl;
    }
    std::cout.precision(8);
    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" creator=\"bin2gpx\" version=\"1.1\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\">\n\t<trk>" << std::endl;
    std::cout << "\t\t<trkseg>" << std::endl;
    for (std::list<osmscout::Point>::const_iterator point=points.begin();
	 point!=points.end();
	 ++point) {
      std::cout << "\t\t\t<trkpt lat=\""<< point->GetLat() << "\" lon=\""<< point->GetLon() <<"\">\n\t\t\t\t</trkpt><fix>2d</fix>" << std::endl;
    }
    std::cout << "\t\t</trkseg>" << std::endl;
    std::cout << "\t</trk>" << std::endl;
    std::cout << "</gpx>" << std::endl;

    return 0;
  }

  if (!postprocessor.PostprocessRouteDescription(description,
                                                 routingProfile,
                                                 database,
                                                 postprocessors)) {
    std::cerr << "Error during route postprocessing" << std::endl;
  }

  std::cout << "----------------------------------------------------" << std::endl;
  std::cout << "     At| After|  Time| After|" << std::endl;
  std::cout << "----------------------------------------------------" << std::endl;
  std::list<osmscout::RouteDescription::Node>::const_iterator prevNode=description.Nodes().end();
  for (std::list<osmscout::RouteDescription::Node>::const_iterator node=description.Nodes().begin();
       node!=description.Nodes().end();
       ++node) {
    osmscout::RouteDescription::DescriptionRef                 desc;
    osmscout::RouteDescription::NameDescriptionRef             nameDescription;
    osmscout::RouteDescription::DirectionDescriptionRef        directionDescription;
    osmscout::RouteDescription::NameChangedDescriptionRef      nameChangedDescription;
    osmscout::RouteDescription::CrossingWaysDescriptionRef     crossingWaysDescription;

    osmscout::RouteDescription::StartDescriptionRef            startDescription;
    osmscout::RouteDescription::TargetDescriptionRef           targetDescription;
    osmscout::RouteDescription::TurnDescriptionRef             turnDescription;
    osmscout::RouteDescription::RoundaboutEnterDescriptionRef  roundaboutEnterDescription;
    osmscout::RouteDescription::RoundaboutLeaveDescriptionRef  roundaboutLeaveDescription;
    osmscout::RouteDescription::MotorwayEnterDescriptionRef    motorwayEnterDescription;
    osmscout::RouteDescription::MotorwayChangeDescriptionRef   motorwayChangeDescription;
    osmscout::RouteDescription::MotorwayLeaveDescriptionRef    motorwayLeaveDescription;

    desc=node->GetDescription(osmscout::RouteDescription::WAY_NAME_DESC);
    if (desc.Valid()) {
      nameDescription=dynamic_cast<osmscout::RouteDescription::NameDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::DIRECTION_DESC);
    if (desc.Valid()) {
      directionDescription=dynamic_cast<osmscout::RouteDescription::DirectionDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::WAY_NAME_CHANGED_DESC);
    if (desc.Valid()) {
      nameChangedDescription=dynamic_cast<osmscout::RouteDescription::NameChangedDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::CROSSING_WAYS_DESC);
    if (desc.Valid()) {
      crossingWaysDescription=dynamic_cast<osmscout::RouteDescription::CrossingWaysDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::NODE_START_DESC);
    if (desc.Valid()) {
      startDescription=dynamic_cast<osmscout::RouteDescription::StartDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::NODE_TARGET_DESC);
    if (desc.Valid()) {
      targetDescription=dynamic_cast<osmscout::RouteDescription::TargetDescription*>(desc.Get());
    }


    desc=node->GetDescription(osmscout::RouteDescription::TURN_DESC);
    if (desc.Valid()) {
      turnDescription=dynamic_cast<osmscout::RouteDescription::TurnDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::ROUNDABOUT_ENTER_DESC);
    if (desc.Valid()) {
      roundaboutEnterDescription=dynamic_cast<osmscout::RouteDescription::RoundaboutEnterDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::ROUNDABOUT_LEAVE_DESC);
    if (desc.Valid()) {
      roundaboutLeaveDescription=dynamic_cast<osmscout::RouteDescription::RoundaboutLeaveDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::MOTORWAY_ENTER_DESC);
    if (desc.Valid()) {
      motorwayEnterDescription=dynamic_cast<osmscout::RouteDescription::MotorwayEnterDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::MOTORWAY_CHANGE_DESC);
    if (desc.Valid()) {
      motorwayChangeDescription=dynamic_cast<osmscout::RouteDescription::MotorwayChangeDescription*>(desc.Get());
    }

    desc=node->GetDescription(osmscout::RouteDescription::MOTORWAY_LEAVE_DESC);
    if (desc.Valid()) {
      motorwayLeaveDescription=dynamic_cast<osmscout::RouteDescription::MotorwayLeaveDescription*>(desc.Get());
    }

    if (crossingWaysDescription.Valid() &&
        roundaboutCrossingCounter>0 &&
        crossingWaysDescription->GetExitCount()>1) {
      roundaboutCrossingCounter+=crossingWaysDescription->GetExitCount()-1;
    }

    if (!HasRelevantDescriptions(*node)) {
      continue;
    }

#if defined(HTML)
    std::cout << "<tr><td>";
#endif
    std::cout << std::setfill(' ') << std::setw(5) << std::fixed << std::setprecision(1);
    std::cout << node->GetDistance() << "km ";

#if defined(HTML)
    std::cout <<"</td><td>";
#endif

    if (prevNode!=description.Nodes().end() && node->GetDistance()-prevNode->GetDistance()!=0.0) {
      std::cout << std::setfill(' ') << std::setw(4) << std::fixed << std::setprecision(1);
      std::cout << node->GetDistance()-prevNode->GetDistance() << "km ";
    }
    else {
      std::cout << "       ";
    }

#if defined(HTML)
    std::cout << "<tr><td>";
#endif
    std::cout << TimeToString(node->GetTime()) << "h ";

#if defined(HTML)
    std::cout <<"</td><td>";
#endif

    if (prevNode!=description.Nodes().end() && node->GetTime()-prevNode->GetTime()!=0.0) {
      std::cout << TimeToString(node->GetTime()-prevNode->GetTime()) << "h ";
    }
    else {
      std::cout << "       ";
    }


#if defined(HTML)
    std::cout <<"</td><td>";
#endif

    size_t lineCount=0;

#if defined(ROUTE_DEBUG) || defined(NODE_DEBUG)
    NextLine(lineCount);

    std::cout << "// " << node->GetTime() << "h " << std::setw(0) << std::setprecision(3) << node->GetDistance() << "km ";

    if (node->GetPathObject().Valid()) {
      std::cout << node->GetPathObject().GetTypeName() << " " << node->GetPathObject().GetFileOffset() << "[" << node->GetCurrentNodeIndex() << "] => " << node->GetPathObject().GetTypeName() << " " << node->GetPathObject().GetFileOffset() << "[" << node->GetTargetNodeIndex() << "]";
    }

    std::cout << std::endl;

    for (std::list<osmscout::RouteDescription::DescriptionRef>::const_iterator d=node->GetDescriptions().begin();
        d!=node->GetDescriptions().end();
        ++d) {
      osmscout::RouteDescription::DescriptionRef desc=*d;

      NextLine(lineCount);
      std::cout << "// " << desc->GetDebugString() << std::endl;
    }
#endif

    if (startDescription.Valid()) {
      DumpStartDescription(lineCount,
                           startDescription,
                           nameDescription);
    }
    else if (targetDescription.Valid()) {
      DumpTargetDescription(lineCount,targetDescription);
    }
    else if (turnDescription.Valid()) {
      DumpTurnDescription(lineCount,
                          turnDescription,
                          crossingWaysDescription,
                          directionDescription,
                          nameDescription);
    }
    else if (roundaboutEnterDescription.Valid()) {
      DumpRoundaboutEnterDescription(lineCount,
                                     roundaboutEnterDescription,
                                     crossingWaysDescription);

      roundaboutCrossingCounter=1;
    }
    else if (roundaboutLeaveDescription.Valid()) {
      DumpRoundaboutLeaveDescription(lineCount,
                                     roundaboutLeaveDescription,
                                     nameDescription);

      roundaboutCrossingCounter=0;
    }
    else if (motorwayEnterDescription.Valid()) {
      DumpMotorwayEnterDescription(lineCount,
                                   motorwayEnterDescription,
                                   crossingWaysDescription);
    }
    else if (motorwayChangeDescription.Valid()) {
      DumpMotorwayChangeDescription(lineCount,
                                    motorwayChangeDescription);
    }
    else if (motorwayLeaveDescription.Valid()) {
      DumpMotorwayLeaveDescription(lineCount,
                                   motorwayLeaveDescription,
                                   directionDescription,
                                   nameDescription);
    }
    else if (nameChangedDescription.Valid()) {
      DumpNameChangedDescription(lineCount,
                                 nameChangedDescription);
    }

    if (lineCount==0) {
      std::cout << std::endl;
    }

#if defined(HTML)
    std::cout << "</td></tr>";
#endif

    prevNode=node;
  }

  router.Close();

  return 0;
}
