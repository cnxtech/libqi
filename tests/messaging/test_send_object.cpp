/*
**
** Copyright (C) 2012 Aldebaran Robotics
*/

#include <gtest/gtest.h>
#include <qi/application.hpp>
#include <qi/anyobject.hpp>
#include <qi/session.hpp>
#include <qi/signalspy.hpp>
#include <qi/anymodule.hpp>
#include <testsession/testsessionpair.hpp>
#include <qi/testutils/testutils.hpp>
#include <condition_variable>

qiLogCategory("test");

int timeoutMs = 300;
qi::Duration timeout = qi::MilliSeconds(timeoutMs);

void test_service(const qi::AnyObject &o)
{
  ASSERT_TRUE(o);
  int res = o.call<int>("testMethod", 12);
  ASSERT_EQ(13, res);
}

TEST(SendObject, pass_obj)
{
  TestSessionPair p;

  qi::SessionPtr s = p.server();

  s->loadService("naoqi.testanymodule.test");
  qi::AnyObject o = s->service("test");

  qiLogInfo() << "check first service" << std::endl;
  test_service(o);

  qi::AnyObject o2 = o.call<qi::AnyObject>("make_other_object");

  s->registerService("test_bis", o2);

  std::cout << "check second service" << std::endl;
  test_service(o2);
  qi::AnyObject o3 = s->service("test_bis");
  test_service(o3);

  s->close();
}

TEST(SendObject, load_received_obj)
{
  TestSessionPair p;

  qi::SessionPtr s = p.server();

  s->loadService("naoqi.testanymodule.test");
  qi::AnyObject o = s->service("test");
  test_service(o);

  s->registerService("test_bis", o);
  qi::AnyObject o2 = s->service("test_bis");
  test_service(o2);

  s->close();
}

TEST(SendObject, unregister_obj)
{
  TestSessionPair p;

  qi::SessionPtr s = p.server();

  int index = s->loadService("naoqi.testanymodule.test");
  qi::AnyObject o = s->service("test");
  test_service(o);

  s->unregisterService(index).wait();
  ASSERT_ANY_THROW(s->service("test"));

  s->close();
}

class SubObjectToPing
{
public:
  void subping() { qiLogInfo() << "subping !"; }
};

QI_REGISTER_OBJECT(SubObjectToPing, subping)

class ObjectToPing
{
public:
  ObjectToPing() { propToPing.set(boost::make_shared<SubObjectToPing>()); }
  void ping() { qiLogInfo() << "ping !" << std::endl; }
  qi::Property<qi::AnyObject> propToPing;
};

QI_REGISTER_OBJECT(ObjectToPing, ping, propToPing)

class ObjectEmitter
{
public:
  ObjectEmitter()
  {
    object.set(boost::make_shared<ObjectToPing>());
  }

  void emitObject(qi::AnyObject o) { QI_EMIT onTruc(o); }
  qi::AnyObject identity(qi::AnyObject o) {return o;}
  void receiveObject(qi::AnyObject o)
  {
    auto gettingProperty = o.property<void>("propToPing");
    auto state = gettingProperty.waitFor(timeout);
    std::stringstream errorMessage;
    errorMessage << "Unexpected state";
    if (state == qi::FutureState_FinishedWithError)
      errorMessage << ", error: " << gettingProperty.error();
    else
      errorMessage << ": " << state;
    EXPECT_EQ(qi::FutureState_FinishedWithValue, state) << errorMessage.str();
  }

  std::vector<qi::AnyObject> identities(const std::vector<qi::AnyObject>& objects)
  {
    return objects;
  }

  std::vector<qi::AnyObject> generateObjects()
  {
    return std::vector<qi::AnyObject>{boost::make_shared<ObjectToPing>()};
  }

  qi::Signal<qi::AnyObject> onTruc;
  qi::Property<qi::AnyObject> object;
  qi::Property<std::vector<qi::AnyObject>> vectorOfObjects;
};

QI_REGISTER_OBJECT(ObjectEmitter, emitObject, identity, identities, generateObjects,
                   receiveObject, onTruc, vectorOfObjects, object)


TEST(SendObject, pass_obj_made_from_module)
{
  qi::AnyModule testModule = qi::import("naoqi.testanymodule");
  auto obj = testModule.call<qi::AnyObject>("test");
  auto obj2 = obj;
  ASSERT_EQ(obj, obj2);
  ASSERT_EQ(1, obj.call<int>("testMethod", 0)); // just checking, in case of

  TestSessionPair p;
  p.server()->registerService("plop", boost::make_shared<ObjectEmitter>());

  qi::AnyObject remotePlop = p.client()->service("plop");
  qi::Promise<void> receivingObject;
  auto signalLink = remotePlop.connect(
        "onTruc", boost::function<void(qi::AnyObject)>([=](qi::AnyObject o) mutable
  {
    ASSERT_EQ(o, obj);
    ASSERT_EQ(1, o.call<int>("testMethod", 0)); // this is the real test
    receivingObject.setValue(0);
  })).value();
  remotePlop.async<void>("emitObject", obj);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, receivingObject.future().waitFor(timeout));
  // If the test failed and the onTruc signal has not been triggered before the
  // end of the timeout, it could still be called during the test destruction.
  // Disconnect the callback to make sure it is not called after receivingObject
  // Promise has been destroyed.
  remotePlop.disconnect(signalLink);
}

TEST(SendObject, IdentityOfRemoteObjects)
{
  TestSessionPair p;
  p.server()->registerService("plop", boost::make_shared<ObjectEmitter>());

  qi::DynamicObjectBuilder builder;
  auto o = builder.object();

  qi::AnyObject remotePlop = p.client()->service("plop");
  auto remoteObject = remotePlop.call<qi::AnyObject>("identity", o);
  EXPECT_EQ(o, remoteObject);

  qi::DynamicObjectBuilder builder2;
  auto o2 = builder2.object();
  auto remoteObject2 = remotePlop.call<qi::AnyObject>("identity", o2);
  EXPECT_EQ(o2, remoteObject2);
  EXPECT_NE(o2, remoteObject);
  EXPECT_NE(remoteObject2, remoteObject);
  EXPECT_NE(remoteObject2, o);
}

static int next_dummy_id = 0;
struct dummy_t
{
  int value = next_dummy_id++;
  int one() const
  {
    return value;
  }
};

QI_REGISTER_OBJECT(dummy_t, one);

TEST(SendObject, IdentityOfRemoteObjectsDifferentProcess)
{
  using namespace qi;

  const Url serviceUrl{"tcp://127.0.0.1:54321"};
  test::ScopedProcess _{path::findBin("remoteserviceowner"),
    {"--qi-standalone", "--qi-listen-url=" + serviceUrl.str()}
  };

  auto client = makeSession();
  client->connect(serviceUrl);
  AnyObject service = client->service("PingPongService");
  AnyObject original{boost::make_shared<dummy_t>()};

  service.call<void>("give", original);
  AnyObject copy0 = service.call<AnyObject>("take");
  EXPECT_EQ(copy0, original);

  service.call<void>("give", copy0);
  AnyObject copy1 = service.call<AnyObject>("take");
  ASSERT_EQ(copy1, copy0);
  ASSERT_EQ(copy1, original);

  ASSERT_EQ(copy1.call<int>("one"), copy0.call<int>("one"));
}


class ObjectStore
{
  qi::AnyObject obj;
public:
  qi::AnyObject get() const
  {
    return obj;
  }
  void set(qi::AnyObject o)
  {
    obj = o;
  }
};

QI_REGISTER_OBJECT(ObjectStore, get, set);

TEST(SendObject, IdentityMaintainedBetweenSessions)
{
  TestSessionPair sessionPair;
  auto original_store = boost::make_shared<ObjectStore>();
  sessionPair.server()->registerService("store", original_store);

  auto object = qi::AnyObject{boost::make_shared<dummy_t>()};
  original_store->set(object);

  auto store_from_server = sessionPair.server()->service("store").value();
  auto store_from_client = sessionPair.client()->service("store").value();

  qi::AnyObject object_from_server_1 = store_from_server.call<qi::AnyObject>("get");
  qi::AnyObject object_from_server_2 = store_from_server.call<qi::AnyObject>("get");

  qi::AnyObject object_from_client_1 = store_from_client.call<qi::AnyObject>("get");
  qi::AnyObject object_from_client_2 = store_from_client.call<qi::AnyObject>("get");

  EXPECT_EQ(object_from_server_1, object);
  EXPECT_EQ(object_from_server_2, object);
  EXPECT_EQ(object_from_server_1, object_from_server_2);

  EXPECT_EQ(object_from_client_1, object);
  EXPECT_EQ(object_from_client_2, object);
  EXPECT_EQ(object_from_client_1, object_from_client_2);

  EXPECT_EQ(object_from_server_1, object_from_client_1);
  EXPECT_EQ(object_from_server_1, object_from_client_2);
  EXPECT_EQ(object_from_server_2, object_from_client_1);
  EXPECT_EQ(object_from_server_2, object_from_client_2);

  qi::Session outterSession;
  outterSession.connect(sessionPair.endpointToServiceSource());
  qi::AnyObject store_from_outter = outterSession.service("store");
  qi::AnyObject object_from_outter_1 = store_from_outter.call<qi::AnyObject>("get");
  qi::AnyObject object_from_outter_2 = store_from_outter.call<qi::AnyObject>("get");

  EXPECT_EQ(object_from_outter_1, object);
  EXPECT_EQ(object_from_outter_2, object);
  EXPECT_EQ(object_from_outter_1, object_from_outter_2);

  EXPECT_EQ(object_from_outter_1, object_from_client_1);
  EXPECT_EQ(object_from_outter_1, object_from_client_2);
  EXPECT_EQ(object_from_outter_2, object_from_client_1);
  EXPECT_EQ(object_from_outter_2, object_from_client_2);

  EXPECT_EQ(object_from_outter_1, object_from_server_1);
  EXPECT_EQ(object_from_outter_1, object_from_server_2);
  EXPECT_EQ(object_from_outter_2, object_from_server_1);
  EXPECT_EQ(object_from_outter_2, object_from_server_2);

}

TEST(SendObject, IdentityMaintainedBetweenSessionsWithRemote)
{
  using namespace qi;

  const Url serviceUrl{ "tcp://127.0.0.1:54321" };
  test::ScopedProcess _{ path::findBin("remoteserviceowner"),
  { "--qi-standalone", "--qi-listen-url=" + serviceUrl.str() }
  };

  auto client = makeSession();
  client->connect(serviceUrl);
  AnyObject store_from_client = client->service("PingPongService");

  auto object = qi::AnyObject{ boost::make_shared<dummy_t>() };
  store_from_client.call<void>("give", object);

  AnyObject object_from_client_1 = store_from_client.call<AnyObject>("take");
  AnyObject object_from_client_2 = store_from_client.call<AnyObject>("take");

  EXPECT_EQ(object_from_client_1, object);
  EXPECT_EQ(object_from_client_2, object);
  EXPECT_EQ(object_from_client_1, object_from_client_2);

  Session outterSession;
  outterSession.connect(serviceUrl);
  AnyObject store_from_outter = outterSession.service("PingPongService");
  AnyObject object_from_outter_1 = store_from_outter.call<AnyObject>("take");
  AnyObject object_from_outter_2 = store_from_outter.call<AnyObject>("take");

  EXPECT_EQ(object_from_outter_1, object);
  EXPECT_EQ(object_from_outter_2, object);
  EXPECT_EQ(object_from_outter_1, object_from_outter_2);

  EXPECT_EQ(object_from_outter_1, object_from_client_1);
  EXPECT_EQ(object_from_outter_1, object_from_client_2);
  EXPECT_EQ(object_from_outter_2, object_from_client_1);
  EXPECT_EQ(object_from_outter_2, object_from_client_2);

}


TEST(SendObject, IdentityOfRemoteObjectsMoreIndirections)
{
  qi::AnyObject originalObject(boost::make_shared<dummy_t>());
  TestSessionPair pairA;
  pairA.server()->registerService("serviceA", boost::make_shared<ObjectStore>());
  qi::AnyObject clientA = pairA.client()->service("serviceA");
  clientA.call<void>("set", originalObject);
  qi::AnyObject objA = clientA.call<qi::AnyObject>("get");
  EXPECT_EQ(originalObject, objA);

  TestSessionPair pairB;
  pairB.server()->registerService("serviceB", boost::make_shared<ObjectStore>());
  qi::AnyObject clientB = pairB.client()->service("serviceB");
  clientB.call<void>("set", objA);
  qi::AnyObject objB = clientB.call<qi::AnyObject>("get");
  EXPECT_EQ(originalObject, objB);

  TestSessionPair pairC;
  pairC.server()->registerService("serviceC", boost::make_shared<ObjectStore>());
  qi::AnyObject clientC = pairC.client()->service("serviceC");
  clientC.call<void>("set", objB);
  qi::AnyObject objC = clientC.call<qi::AnyObject>("get");
  EXPECT_EQ(originalObject, objC);
  EXPECT_EQ(objA, objC);
  EXPECT_EQ(objB, objC);
}

////////////////////////////////////////////////////////////////////////////
// The following tests check that object identity is maintained when
// using the interface/proxy/impl system of libqi.
// An interface type is always registered with an associated proxy type.
// The proxy type will be instantiated when receiving an object of the
// interface type. The real implementation can be any type compatible
// with the interface type.
// Here we want to make sure that the ptruid of the real implementation
// object is propagated to all its proxies in all kinds of situations
// so that it is always possible to identify if a proxy is actually representing
// the same implementation object.
//////

struct SomeInterface
{
  virtual ~SomeInterface() = default;
  virtual int get() const = 0;
};

QI_REGISTER_OBJECT(SomeInterface, get)

struct SomeInterfaceProxy : SomeInterface, public qi::Proxy
{
  SomeInterfaceProxy(qi::AnyObject o)
    : qi::Proxy(o) {}

  int get() const override { return asObject().call<int>("get"); }
};
QI_REGISTER_PROXY_INTERFACE(SomeInterfaceProxy, SomeInterface);

static std::atomic<int> nextSomeInterfaceId{ 0 };

class SomeInterfaceImpl : public SomeInterface
{
  int id = nextSomeInterfaceId++;
public:
  int get() const override { return id; }
};
QI_REGISTER_OBJECT(SomeInterfaceImpl, get)


TEST(SendObjectInterfaceProxy, IdentityDependsOnObjectAddressWithAnyObject)
{
  using namespace qi;
  auto realObject = boost::make_shared<SomeInterfaceImpl>();
  const PtrUid ptruid = os::ptrUid(realObject.get());
  Object<SomeInterface> a{ AnyObject{ realObject } };
  Object<SomeInterface> b{ AnyObject{ realObject } };

  EXPECT_EQ(ptruid, a.ptrUid());
  EXPECT_EQ(a, b);
  EXPECT_EQ(a->get(), b->get());
}

TEST(SendObjectInterfaceProxy, IdentityDependsOnObjectAddressWithObjectT)
{
  using namespace qi;
  auto realObject = boost::make_shared<SomeInterfaceImpl>();
  const PtrUid ptruid = os::ptrUid(realObject.get());
  Object<SomeInterface> a{ realObject };
  Object<SomeInterface> b{ realObject };

  EXPECT_EQ(ptruid, a.ptrUid());
  EXPECT_EQ(a, b);
  EXPECT_EQ(a->get(), b->get());
}


TEST(SendObjectInterfaceProxy, IdentityIsMaintainedWhenSentToRemoteAnyObjectStoreRetrievingAnyObject)
{
  using namespace qi;

  TestSessionPair sessions;

  AnyObject original{ boost::make_shared<SomeInterfaceImpl>() };
  sessions.server()->registerService("Store", boost::make_shared<ObjectStore>());
  AnyObject store = sessions.client()->service("Store");
  store.call<void>("set", original);

  Object<SomeInterface> objectA = store.call<AnyObject>("get");
  EXPECT_EQ(original, objectA) << "original ptruid: {" << original.ptrUid() << "}; objectA ptruid: {" << objectA.ptrUid() << "};";

}

TEST(SendObjectInterfaceProxy, IdentityIsMaintainedWhenSentToRemoteAnyObjectStoreRetrievingObjectT)
{
  using namespace qi;

  TestSessionPair sessions;

  AnyObject original{ boost::make_shared<SomeInterfaceImpl>() };
  sessions.server()->registerService("Store", boost::make_shared<ObjectStore>());
  AnyObject store = sessions.client()->service("Store");
  store.call<void>("set", original);

  Object<SomeInterface> objectA = store.call<Object<SomeInterface>>("get");
  EXPECT_EQ(original, objectA) << "original ptruid: {" << original.ptrUid() <<"}; vs objectA ptruid: {" << objectA.ptrUid() << "};";

}

struct SomeStore
{
  virtual qi::Object<SomeInterface> get() const = 0;
  virtual void set(qi::Object<SomeInterface> o) = 0;
};
QI_REGISTER_OBJECT(SomeStore, get, set);

class SomeStoreProxy : public SomeStore, public qi::Proxy
{
public:
  SomeStoreProxy(qi::AnyObject o)
    : qi::Proxy(o) {}

  qi::Object<SomeInterface> get() const override
  {
    return asObject().call<qi::Object<SomeInterface>>("get");
  }
  void set(qi::Object<SomeInterface> o) override
  {
    return asObject().call<void>("set", o);
  }
};
QI_REGISTER_PROXY_INTERFACE(SomeStoreProxy, SomeStore);

class SomeStoreImpl : SomeStore
{
  qi::Object<SomeInterface> obj;
public:
  qi::Object<SomeInterface> get() const override
  {
    return obj;
  }
  void set(qi::Object<SomeInterface> o) override
  {
    obj = o;
  }
};
QI_REGISTER_OBJECT(SomeStoreImpl, get, set);

TEST(SomeInterface, IdentityIsMaintainedWhenSentToInterfaceSpecializedStoreRetrievingAnyObject)
{
  using namespace qi;

  TestSessionPair sessions;

  Object<SomeInterface> original{ boost::make_shared<SomeInterfaceImpl>() };
  sessions.server()->registerService("Store", boost::make_shared<SomeStoreImpl>());
  Object<SomeStore> store = sessions.client()->service("Store");
  store->set(original);

  Object<SomeInterface> objectA = store->get();
  EXPECT_EQ(original, objectA) << "original ptruid: {" << original.ptrUid() << "}; vs objectA ptruid: {" << objectA.ptrUid() << "};";

}



TEST(SomeInterface, IdentityIsMaintainedWhenSentToRemoteProcessAnyObjectStoreRetrievingAnyObject)
{
  using namespace qi;

  const Url serviceUrl{ "tcp://127.0.0.1:54321" };
  test::ScopedProcess _{ path::findBin("remoteserviceowner"),
  { "--qi-standalone", "--qi-listen-url=" + serviceUrl.str() }
  };

  auto client = makeSession();
  client->connect(serviceUrl);
  AnyObject service = client->service("PingPongService");
  Object<SomeInterface> original{ boost::make_shared<SomeInterfaceImpl>() };

  service.call<void>("give", original);
  AnyObject copy0 = service.call<AnyObject>("take");
  EXPECT_EQ(copy0, original) << "copy0 ptruid: {" << copy0.ptrUid() << "}; vs original ptruid: {" << original.ptrUid() << "};";

  service.call<void>("give", copy0);
  AnyObject copy1 = service.call<AnyObject>("take");
  EXPECT_EQ(copy1, copy0) << "copy1 ptruid: {" << copy1.ptrUid() << "}; vs copy0 ptruid: {" << copy0.ptrUid() << "};";
  EXPECT_EQ(copy1, original) << "copy1 ptruid: {" << copy1.ptrUid() << "}; vs original ptruid: {" << original.ptrUid() << "};";
}



TEST(SomeInterface, IdentityIsMaintainedWhenSentToRemoteProcessAnyObjectStoreRetrievingObjectT)
{
  using namespace qi;

  const Url serviceUrl{ "tcp://127.0.0.1:54321" };
  test::ScopedProcess _{ path::findBin("remoteserviceowner"),
  { "--qi-standalone", "--qi-listen-url=" + serviceUrl.str() }
  };

  auto client = makeSession();
  client->connect(serviceUrl);
  AnyObject service = client->service("PingPongService");
  Object<SomeInterface> original{ boost::make_shared<SomeInterfaceImpl>() };

  service.call<void>("give", original);
  Object<SomeInterface> copy0 = service.call<Object<SomeInterface>>("take");
  EXPECT_EQ(copy0, original) << "copy0 ptruid: {" << copy0.ptrUid() << "}; vs original ptruid: {" << original.ptrUid() << "};";

  service.call<void>("give", copy0);
  Object<SomeInterface> copy1 = service.call<Object<SomeInterface>>("take");
  EXPECT_EQ(copy1, copy0) << "copy1 ptruid: {" << copy1.ptrUid() << "}; vs copy0 ptruid: {" << copy0.ptrUid() << "};";
  EXPECT_EQ(copy1, original) << "copy1 ptruid: {" << copy1.ptrUid() << "}; vs original ptruid: {" << original.ptrUid() << "};";
}

////////
// End of tests about object identification with interface/proxy/impl system.
////////////////////////////////////////////////////////////////////////////////

class ObjectEmitterFactory
{
public:
  qi::AnyObject makeObjectEmitter() { return boost::make_shared<ObjectEmitter>(); }
};

QI_REGISTER_OBJECT(ObjectEmitterFactory, makeObjectEmitter)

TEST(SendObject, pass_obj_made_from_module_to_an_obj_made_from_service)
{
  qi::AnyModule testModule = qi::import("naoqi.testanymodule");
  auto obj = testModule.call<qi::AnyObject>("test");
  ASSERT_EQ(1, obj.call<int>("testMethod", 0)); // just checking, in case of

  TestSessionPair p;
  p.server()->registerService("EmitterFactory", boost::make_shared<ObjectEmitterFactory>());

  qi::AnyObject emitterFactory = p.client()->service("EmitterFactory");
  auto emitter = emitterFactory.call<qi::AnyObject>("makeObjectEmitter");

  qi::Promise<void> receivingObject;
  emitter.connect("onTruc", boost::function<void(qi::AnyObject)>([=](qi::AnyObject o) mutable
  {
    ASSERT_EQ(o, obj);
    int i = o.call<int>("testMethod", 0);
    ASSERT_EQ(1, i); // this is the real test
    receivingObject.setValue(0);
  }));
  emitter.async<void>("emitObject", obj);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, receivingObject.future().waitFor(timeout));
}

TEST(SendObject, emitter_from_factory_transmits_objects_through_property_then_receive_object)
{
  TestSessionPair p;
  p.server()->registerService("EmitterFactory", boost::make_shared<ObjectEmitterFactory>());

  qi::AnyObject emitterFactory = p.client()->service("EmitterFactory");
  auto emitter = emitterFactory.call<qi::AnyObject>("makeObjectEmitter");

  // create vector
  std::vector<qi::AnyObject> vecObj{
    boost::make_shared<ObjectToPing>(), boost::make_shared<ObjectToPing>()
  };

  emitter.setProperty<std::vector<qi::AnyObject>>("vectorOfObjects", vecObj);
  auto vectorOfObjectsReceived = emitter.property<std::vector<qi::AnyObject>>("vectorOfObjects").value();
  auto objectToReceive = vectorOfObjectsReceived[0];
  auto receiving = emitter.async<void>("receiveObject", objectToReceive);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, receiving.wait(timeoutMs));
}

TEST(SendObject, emitter_from_factory_transmits_objects_through_property_then_ping_property)
{
  TestSessionPair p;
  p.server()->registerService("EmitterFactory", boost::make_shared<ObjectEmitterFactory>());

  qi::AnyObject emitterFactory = p.client()->service("EmitterFactory");
  auto emitter = emitterFactory.call<qi::AnyObject>("makeObjectEmitter");

  // create vector
  std::vector<qi::AnyObject> vecObj{
    boost::make_shared<ObjectToPing>(), boost::make_shared<ObjectToPing>()
  };

  emitter.setProperty<std::vector<qi::AnyObject>>("vectorOfObjects", vecObj);
  auto vectorOfObjectsReceived = emitter.property<std::vector<qi::AnyObject>>("vectorOfObjects").value();
  auto objectToPing = vectorOfObjectsReceived[0];
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objectToPing.property<void>("propToPing").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_transmits_objects_through_property_then_ping_property)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());
  qi::AnyObject emitter = p.client()->service("ObjectEmitter");

  // create vector
  std::vector<qi::AnyObject> vecObj{
    boost::make_shared<ObjectToPing>(), boost::make_shared<ObjectToPing>()
  };

  emitter.setProperty<std::vector<qi::AnyObject>>("vectorOfObjects", vecObj);
  auto vectorOfObjectsReceived = emitter.property<std::vector<qi::AnyObject>>("vectorOfObjects").value();
  auto objectToPing = vectorOfObjectsReceived[0];
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objectToPing.property<void>("propToPing").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_identities_then_ping_property)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());

  qi::AnyObject emitter = p.client()->service("ObjectEmitter");

  // create vector
  std::vector<qi::AnyObject> vecObj{
    boost::make_shared<ObjectToPing>(), boost::make_shared<ObjectToPing>()
  };

  auto sameObjects = emitter.call<std::vector<qi::AnyObject>>("identities", vecObj);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, sameObjects[0].property<void>("propToPing").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_identities_then_ping)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());

  qi::AnyObject emitter = p.client()->service("ObjectEmitter");

  // create vector
  std::vector<qi::AnyObject> vecObj{
    boost::make_shared<ObjectToPing>(), boost::make_shared<ObjectToPing>()
  };

  auto sameObjects = emitter.call<std::vector<qi::AnyObject>>("identities", vecObj);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, sameObjects[0].async<void>("ping").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_generate_objects_then_ping)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());
  qi::AnyObject emitter = p.client()->service("ObjectEmitter");
  auto objects = emitter.call<std::vector<qi::AnyObject>>("generateObjects");
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objects[0].async<void>("ping").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_identity_then_ping)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());

  qi::AnyObject emitter = p.client()->service("ObjectEmitter");
  qi::AnyObject object{boost::make_shared<ObjectToPing>()};

  auto sameObject = emitter.call<qi::AnyObject>("identity", object);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, sameObject.async<void>("ping").wait(timeoutMs));
}

TEST(SendObject, emitter_from_factory_transmits_single_object_through_property_then_receive_object)
{
  TestSessionPair p;
  p.server()->registerService("EmitterFactory", boost::make_shared<ObjectEmitterFactory>());

  qi::AnyObject emitterFactory = p.client()->service("EmitterFactory");
  auto emitter = emitterFactory.call<qi::AnyObject>("makeObjectEmitter");

  // create vector
  qi::AnyObject objToPing = boost::make_shared<ObjectToPing>();

  emitter.setProperty<qi::AnyObject>("object", objToPing);
  auto objectStored = emitter.property<qi::AnyObject>("object").value();
  auto receiving = emitter.async<void>("receiveObject", objectStored);
  ASSERT_EQ(qi::FutureState_FinishedWithValue, receiving.wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_transmits_single_object_through_property_then_ping_property)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());

  qi::AnyObject emitter = p.client()->service("ObjectEmitter");
  qi::AnyObject objToPing = boost::make_shared<ObjectToPing>();

  emitter.setProperty<qi::AnyObject>("object", objToPing);
  auto objectStored = emitter.property<qi::AnyObject>("object").value();
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objectStored.property<void>("propToPing").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_transmits_single_object_through_property_then_ping)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());
  qi::AnyObject emitter = p.client()->service("ObjectEmitter");

  qi::AnyObject objToPing = boost::make_shared<ObjectToPing>();
  emitter.setProperty<qi::AnyObject>("object", objToPing);
  auto objectStored = emitter.property<qi::AnyObject>("object").value();
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objectStored.async<void>("ping").wait(timeoutMs));
}

TEST(SendObject, object_emitter_service_provides_single_object_through_property_then_ping)
{
  TestSessionPair p;
  p.server()->registerService("ObjectEmitter", boost::make_shared<ObjectEmitter>());
  qi::AnyObject emitter = p.client()->service("ObjectEmitter");
  auto objectStored = emitter.property<qi::AnyObject>("object").value();
  ASSERT_EQ(qi::FutureState_FinishedWithValue, objectStored.async<void>("ping").wait(timeoutMs));
}

class Human
{
public:
  void pingMe(const qi::AnyObject& humanToPing) {std::string oName = humanToPing.call<std::string>("ping");}
  std::string ping()
  {
    qiLogInfo() << "Ping !";
    return "human";
  }

  void nameMe(const std::string& newName) {name.set(newName);}
  qi::Property<std::string> name;
};
QI_REGISTER_OBJECT(Human, pingMe, ping, nameMe, name)

class Actuation
{
public:
  Actuation() { humanProperty.set(boost::make_shared<Human>()); }

  qi::AnyObject getHomeMadeHuman() { return boost::make_shared<Human>(); }

  void set(const qi::AnyObject& humanToSet) {_human = humanToSet;}
  qi::AnyObject get() const {return _human;}

private:
  qi::AnyObject _human;

public:
  void emitHumanProperty()
  {
    humanProperty.set(boost::make_shared<Human>());
  }
  qi::Property<qi::AnyObject> humanProperty;

};
QI_REGISTER_OBJECT(Actuation, set, get, getHomeMadeHuman, emitHumanProperty, humanProperty)

TEST(SendObject, reuse_object_taken_from_connect)
{
  TestSessionPair p;
  p.server()->registerService("Actuation", boost::make_shared<Actuation>());
  qi::AnyObject actuation = p.client()->service("Actuation");

  qi::Promise<bool> pinged;
  actuation.connect("humanProperty",
                    boost::function<void(qi::AnyObject)>([&](qi::AnyObject human)
  {
    auto homeMadeHuman = actuation.call<qi::AnyObject>("getHomeMadeHuman");
    human.call<void>("pingMe", homeMadeHuman);
    pinged.setValue(true);
  }));
  actuation.call<void>("emitHumanProperty");

  ASSERT_EQ(qi::FutureState_FinishedWithValue, pinged.future().waitFor(qi::MilliSeconds(2000)));
}

// Check that we can connect to a property exposed on an object retrieved in a property callback
TEST(SendObject, connect_to_object_taken_from_connect)
{
  TestSessionPair p;
  p.server()->registerService("Actuation", boost::make_shared<Actuation>());
  qi::AnyObject actuation = p.client()->service("Actuation");

  qi::Promise<bool> pinged;
  qi::Promise<bool> named;
  actuation.connect("humanProperty",
                    boost::function<void(qi::AnyObject)>([=](qi::AnyObject human) mutable
  {
    // explicitly capture human in the lambda to keep it alive
    human.connect("name",
                  boost::function<void(const std::string&)>([named, human](const std::string&) mutable
    {
      named.setValue(true);
    }));
    human.call<void>("nameMe", "(´・ω・`)");
    pinged.setValue(true);
  }));
  actuation.call<void>("emitHumanProperty");

  ASSERT_EQ(qi::FutureState_FinishedWithValue, pinged.future().waitFor(qi::MilliSeconds(2000)));
  ASSERT_EQ(qi::FutureState_FinishedWithValue, named.future().waitFor(qi::MilliSeconds(2000)));
}

TEST(SendObject, make_frame)
{
  TestSessionPair p;
  p.server()->registerService("Actuation", boost::make_shared<Actuation>());
  qi::AnyObject actuation = p.client()->service("Actuation");

  actuation.call<void>("set", boost::make_shared<Human>());
  auto humanReceived = actuation.call<qi::AnyObject>("get");

  auto homeMadeHuman = actuation.call<qi::AnyObject>("getHomeMadeHuman");

  humanReceived.call<void>("ping"); // <-- this one doesn't hang
  homeMadeHuman.call<void>("pingMe", humanReceived); // <-- this one doesn't hang
  humanReceived.call<void>("pingMe", homeMadeHuman); // <-- this one hangs infinitely
  humanReceived.call<void>("pingMe", humanReceived); // <-- this one hangs infinitely
}

class Cookie
{
public:
  Cookie(bool withTaste) : taste(withTaste) {}
  bool eat()
  {
    eaten();
    qiLogInfo() << "Too late, you ate me";
    return true;
  }
  bool eatRival(qi::AnyObject other)
  {
    qiLogInfo() << "I am bigger than the other cookie";
    return other.call<bool>("eat");
  }

  qi::Property<bool> taste;
  qi::Signal<void> eaten;
};

class CookieBox
{
public:
  qi::AnyObject makeCookie(bool withTaste)
  {
    std::weak_ptr<Token> token{ _token };
    return boost::shared_ptr<Cookie>{
      new Cookie{withTaste},
      [this,token](Cookie* cookie) {
        qiLogInfo() << "Cookie destruction";
        delete cookie;

        if (auto cookieBox = token.lock())
        {
          QI_EMIT cookieLost();
        }
        else
        {
          qiLogWarning() << "CookieBox have been destroyed before all Cookies instances destruction!";
        }
      }
    };
  }

  void give(qi::AnyObject c)
  {
    cookie = c;
  }

  qi::AnyObject take()
  {
    return cookie;
  }

  qi::Signal<void> cookieLost;

private:
  qi::AnyObject cookie;

  struct Token {};
  std::shared_ptr<Token> _token{ std::make_shared<Token>() };
};

struct CookieMonster
{
  void feed(qi::AnyObject cookie)
  {
    cookie.call<bool>("eat");
    qiLogInfo() << "Cookiiiiie!!! Om nom nom nom...";
  }
};

struct CookieMonsterFeeder
{
  void feedMonster(qi::AnyObject cookie, qi::AnyObject cookieMonster)
  {
    cookieMonster.call<void>("feed", cookie);
  }
};

QI_REGISTER_OBJECT(Cookie, eat, eatRival, taste, eaten)
QI_REGISTER_OBJECT(CookieBox, makeCookie, give, take)
QI_REGISTER_OBJECT(CookieMonster, feed)
QI_REGISTER_OBJECT(CookieMonsterFeeder, feedMonster)

TEST(SendObject, give_and_take_object_function)
{
  TestSessionPair p;
  p.server()->registerService("CookieBox", boost::make_shared<CookieBox>());
  qi::AnyObject cookieBoxProxy = p.client()->service("CookieBox");
  qi::AnyObject cookie = cookieBoxProxy.call<qi::AnyObject>("makeCookie", true);
  cookieBoxProxy.call<void>("give", cookie);
  qi::AnyObject takenCookie = cookieBoxProxy.call<qi::AnyObject>("take");
  EXPECT_TRUE(takenCookie.call<bool>("eat"));
  EXPECT_EQ(cookie, takenCookie);
}

TEST(SendObject, give_and_take_object_property)
{
  TestSessionPair p;
  p.server()->registerService("CookieBox", boost::make_shared<CookieBox>());
  qi::AnyObject cookieBoxProxy = p.client()->service("CookieBox");
  qi::AnyObject cookie = cookieBoxProxy.call<qi::AnyObject>("makeCookie", true);
  cookieBoxProxy.call<void>("give", cookie);
  qi::AnyObject takenCookie = cookieBoxProxy.call<qi::AnyObject>("take");
  EXPECT_TRUE(takenCookie.property<bool>("taste").value(timeoutMs));
  EXPECT_EQ(cookie, takenCookie);
}

TEST(SendObject, give_and_take_object_signal)
{
  TestSessionPair p;
  p.server()->registerService("CookieBox", boost::make_shared<CookieBox>());
  qi::AnyObject cookieBoxProxy = p.client()->service("CookieBox");
  qi::AnyObject cookie = cookieBoxProxy.call<qi::AnyObject>("makeCookie", true);
  cookieBoxProxy.call<void>("give", cookie);

  qi::AnyObject takenCookie = cookieBoxProxy.call<qi::AnyObject>("take");
  EXPECT_EQ(cookie, takenCookie);
  qi::Promise<bool> eaten;
  auto connecting =
      takenCookie.connect("eaten", boost::function<void()>([&eaten]
  {
    eaten.setValue(true);
  })).async();
  EXPECT_EQ(qi::FutureState_FinishedWithValue, connecting.waitFor(timeout));

  auto eating = takenCookie.async<bool>("eat");
  EXPECT_TRUE(eating.value(timeoutMs));
  EXPECT_TRUE(eaten.future().value(timeoutMs));
}

TEST(SendObject, two_client_objects_call_each_other_on_service_side)
{
  TestSessionPair p;
  auto cookieFeeder = boost::make_shared<CookieMonsterFeeder>();
  p.server()->registerService("CookieMonsterFeeder", cookieFeeder);

  qi::AnyObject cookieTransmitterRemote = p.client()->service("CookieMonsterFeeder");
  auto transmitting = cookieTransmitterRemote.async<void>(
        "feedMonster", boost::make_shared<Cookie>(false), boost::make_shared<CookieMonster>());
  ASSERT_EQ(qi::FutureState_FinishedWithValue, transmitting.wait(timeoutMs));
}

TEST(SendObject, object_referenced_by_remote_only_is_destroyed_on_disconnection)
{
  TestSessionPair p;
  auto cookieBox = boost::make_shared<CookieBox>();
  p.server()->registerService("CookieBox", cookieBox);
  qi::AnyObject cookieBoxRemote = p.client()->service("CookieBox");

  qiLogInfo() << "Getting a cookie!";
  auto cookie = cookieBoxRemote.call<qi::AnyObject>("makeCookie", true);
  QI_UNUSED(cookie);
  qi::SignalSpy cookieLostSpy{cookieBox->cookieLost};

  qiLogInfo() << "Closing the session.";
  p.client()->close();

  // in this case, the references are only counted using the shared pointers
  if (p.mode() == TestMode::Mode_Direct)
    ASSERT_FALSE(cookieLostSpy.waitUntil(1, timeout));

  // In every other case, a remote object intervenes
  else
    ASSERT_TRUE(cookieLostSpy.waitUntil(1, timeout));
}

TEST(SendObject, object_referenced_by_remote_only_is_destroyed_on_unreference)
{
  TestSessionPair p;
  auto cookieBox = boost::make_shared<CookieBox>();
  p.server()->registerService("CookieBox", cookieBox);
  qi::AnyObject cookieBoxRemote = p.client()->service("CookieBox");
  auto cookie = cookieBoxRemote.call<qi::AnyObject>("makeCookie", true);
  qi::SignalSpy cookieLostSpy{cookieBox->cookieLost};
  cookie.reset();
  ASSERT_TRUE(cookieLostSpy.waitUntil(1, timeout));
}

TEST(SendObject, eat_yourself)
{
  TestSessionPair p;
  auto cookieBox = boost::make_shared<CookieBox>();
  p.server()->registerService("CookieBox", cookieBox);
  qi::AnyObject cookieBoxRemote = p.client()->service("CookieBox");
  auto cookie = cookieBoxRemote.call<qi::AnyObject>("makeCookie", true);
  auto otherCookie = cookieBoxRemote.call<qi::AnyObject>("makeCookie", true);
  auto eatOther = cookie.async<bool>("eatRival", otherCookie);
  eatOther.value();
  // ^^^ This works
  auto eatYourself = cookie.async<bool>("eatRival", cookie);
  eatYourself.value();
  // ^^^ This timeouts because cookie.eat() is never called inside of eatRival.
}

class FocusOwner
{
public:
  FocusOwner() {}
  void doNothing() {}
};
QI_REGISTER_OBJECT(FocusOwner, doNothing)

class Focus
{
  std::condition_variable& _var;
  std::atomic_bool& _sessionClosed;
  std::mutex _mutex;
public:
  boost::weak_ptr<FocusOwner> focusOwner;
  qi::Promise<void> focusPromise;

  Focus(std::condition_variable& var,
        std::atomic_bool& closed) : _var(var), _sessionClosed(closed) {}
  qi::Object<FocusOwner> take()
  {
    std::unique_lock<std::mutex> lock(_mutex);
    _var.wait(lock, [this]() { return _sessionClosed.load() == true; });
    auto fo = boost::make_shared<FocusOwner>();
    focusOwner = fo;
    focusPromise.setValue(nullptr);
    return fo;
  }
};
QI_REGISTER_OBJECT(Focus, take)

TEST(SendObject, sendOnClosedConnection)
{
  if(TestMode::getTestMode() == TestMode::Mode_Direct)
    return; // in direct mode the future will hold the object

  std::condition_variable var;
  std::atomic_bool closed{false};
  auto focus = boost::make_shared<Focus>(var, closed);
  TestSessionPair p;
  p.server()->registerService("Focus", focus);

  qi::AnyObject focusService = p.client()->service("Focus");
  auto future = focusService.async<qi::AnyObject>("take");

  p.client()->close().wait();
  closed.store(true);
  var.notify_all();

  focus->focusPromise.future().wait();
  // at this point the focus object should be thrown away by qi::messaging
  // hypothesis: it takes no longer that 2 seconds
  int i = 0;
  while (!focus->focusOwner.expired() && i < 2000)
  {
    qi::sleepFor(qi::MilliSeconds(1));
    ++i;
  }

  ASSERT_TRUE(focus->focusOwner.expired());
}

struct ObjectWithObjProp
{
  qi::Property<qi::Object<Cookie>> prop;
};
QI_REGISTER_OBJECT(ObjectWithObjProp, prop);

TEST(SendObject, PropertySetWithNullObjectNotifiesSubscribers)
{
  static const auto serviceName = testing::UnitTest::GetInstance()->current_test_info()->test_case_name();

  TestSessionPair p;
  auto server = p.server();

  auto obj = boost::make_shared<ObjectWithObjProp>();
  server->registerService(serviceName, obj);
  qi::AnyObject clientObj = p.client()->service(serviceName);

  qi::Promise<bool> prom;
  clientObj.connect("prop", [=](qi::Object<Cookie> obj) mutable {
    prom.setValue(static_cast<bool>(obj));
  }).value();
  obj->prop.set(qi::Object<Cookie>{});

  auto fut = prom.future();
  ASSERT_TRUE(test::finishesWithValue(fut));
  ASSERT_FALSE(fut.value());
}
