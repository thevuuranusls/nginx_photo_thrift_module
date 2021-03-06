#include "photodb_client.h"
#include "gen-cpp-PhotoDB/PhotoDB.h"

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/Thrift.h>
#include <fstream>
#include <queue> 
#include "concurrent_queue.hpp"
using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace photodb;

typedef struct photodb_client_ {
    PhotoDBClient *content;
    PhotoDBClient *metadata;
} photodb_client;

static ConcurrentQueue<photodb_client> client_queue(100);

//TODO: 
// 1 How to change maxsix depend
// 2 consider checking out of side in GET function -> may don't need because it will wait for available client
extern "C" {

    void init_thrift_connection_pool(unsigned int max_client, char* meta_server_address, int meta_server_port, char* content_server_address, int content_server_port) {
        if (client_queue.size() > 0) return;
        for (unsigned int i = 0; i < max_client; i++) {
            // init a client
            photodb_client *client = new photodb_client;

            boost::shared_ptr<TTransport> content_socket(new TSocket(content_server_address, content_server_port));
            boost::shared_ptr<TTransport> content_transport(new TFramedTransport(content_socket));
            boost::shared_ptr<TProtocol> content_protocol(new TBinaryProtocol(content_transport));
            try {
                content_transport->open();
                PhotoDBClient *client_content = new PhotoDBClient(content_protocol);
                boost::shared_ptr<TTransport> metadata_socket(new TSocket(meta_server_address, meta_server_port));
                boost::shared_ptr<TTransport> metadata_transport(new TFramedTransport(metadata_socket));
                boost::shared_ptr<TProtocol> metadata_protocol(new TBinaryProtocol(metadata_transport));
                try {
                    cout << i << endl;
                    metadata_transport->open();
                    PhotoDBClient *client_metadata = new PhotoDBClient(metadata_protocol);

                    client->content = client_content;
                    client->metadata = client_metadata;

                    // Put to queue
                    client_queue.put(*client, 1, 0);

                } catch (TException& tx) {
                    cout << "Can not Open meta PORT number: " << meta_server_port << endl;
                    cout << "Can not Open meta host : " << meta_server_address << endl;
                    cout << "ERROR OPEN: " << tx.what() << endl;
                }
            } catch (TException& tx) {
                cout << "Can not Open content PORT number: " << content_server_port << endl;
                cout << "Can not Open meta host : " << content_server_address << endl;
                cout << "ERROR OPEN: " << tx.what() << endl;
            }
        }
    }

    return_value *kv_up_get(unsigned long key_get, unsigned long size) {
        try {
            return_value *value_return = new return_value;

            photodb_client *client = new photodb_client;

            client_queue.pop(*client, 0);
            // get metadata
            MetaValueResult metadata_result;
            try {
                client->metadata->getMeta(metadata_result, (long int) key_get); // Should check error
                if (metadata_result.error != 0) {
                    client_queue.put(*client, 1, 0);
                    delete value_return;
                    return NULL;
                }
            } catch (TException& tx) {
                cout << "ERROR GET META: " << tx.what() << endl;
                client_queue.put(*client, 1, 0);
                delete value_return;
                return NULL;
            }

            // get content of image
            ImgValueResult content_result;
            try {
                client->content->getImg(content_result, (long int) key_get, (int) size);
                if (content_result.error != 0) {
                    client_queue.put(*client, 1, 0);
                    delete value_return;
                    return NULL;
                }
            } catch (TException& tx) {
                cout << "ERROR GET META: " << tx.what() << endl;
                client_queue.put(*client, 1, 0);
                delete value_return;
                return NULL;
            }

            client_queue.put(*client, 1, 0);

            char *writable_content = new char[content_result.value.img.size() + 1];
            std::copy(content_result.value.img.begin(), content_result.value.img.end(), writable_content);
            writable_content[content_result.value.img.size() + 1] = '\0';
            // Prepare date to return
            value_return->content = writable_content;
            value_return->size = content_result.value.img.size();

            char *writable_etag = new char[metadata_result.value.etag.size() + 1];
            std::copy(metadata_result.value.etag.begin(), metadata_result.value.etag.end(), writable_etag);
            writable_etag[metadata_result.value.etag.size() + 1] = '\0';
            // Prepare etag to return
            value_return->etag = writable_etag;
            value_return->etag_size = metadata_result.value.etag.size();

            char *writable_contentType = new char[metadata_result.value.contentType.size() + 1];
            std::copy(metadata_result.value.contentType.begin(), metadata_result.value.contentType.end(), writable_contentType);
            writable_contentType[metadata_result.value.contentType.size()] = '\0';
            value_return->contentType = writable_contentType;
            value_return->content_type_size = metadata_result.value.contentType.size();

            return value_return;
        } catch (const exception& e) {
            cerr << "EXCEPTION: " << e.what() << endl;
            return NULL;
        }
    }

    void destroy_ConnectionPool(void) {
        // pop until queue is empty
        photodb_client client;
        while (!client_queue.size() != 0) {
            client_queue.pop(client, 1, 0);
        }
        cout << "release OK" << endl;
    }

} // end extern "C"

//return_value *kv_up_get_(unsigned long key_get, unsigned long size) {
//        try {    
//            return_value *value_return = new return_value;
//            
//            photodb_client *client;
//            client_queue.pop(*client, 0);
//// get metadata
//            MetaValueResult *metadata_result = new MetaValueResult;
//            try {
//                client->metadata->getMeta(*metadata_result, key_get); // Should check error
//                if (metadata_result->error != 0) {
//                    client_queue.put(*client, 1, 0);
//                    delete value_return;
//                    return NULL;
//                }
//            } catch (TException& tx) {
//                cout << "ERROR GET META: " << tx.what() << endl;
//                client_queue.put(*client, 1, 0);
//                delete value_return;
//                return NULL;
//            }
//            
//            ImgValueResult *content_result = new ImgValueResult;
//            try {
//                client->content->getImg(*content_result, key_get, size);
//                if (content_result->error != 0) {
//                    client_queue.put(*client, 1, 0);
//                    delete value_return;
//                    return NULL;
//                }         
//            } catch (TException& tx) {
//                cout << "ERROR GET META: " << tx.what() << endl;
//                client_queue.put(*client, 1, 0);
//                delete value_return;
//                return NULL; 
//            }
//            
//            client_queue.put(*client, 1, 0);
//
//            char *writable_content = new char[content_result->value.img.size() +1];
//            std::copy(content_result->value.img.begin(), content_result->value.img.end(), writable_content);
//            writable_content[content_result->value.img.size() +1] = '\0';
//
//            // Prepare date to return
//            value_return->buf = writable_content;
//            value_return->size = content_result->value.img.size();
//
//            char *writable_etag= new char[metadata_result->value.etag.size() +1];
//            std::copy(metadata_result->value.etag.begin(), metadata_result->value.etag.end(), writable_etag);
//            writable_etag[metadata_result->value.etag.size() +1] = '\0';
//
//            // Prepare etag to return
//            value_return->etag = writable_etag;
//
//            return value_return;
//        } catch (const exception& e) {
//            cerr << "EXCEPTION: " << e.what() << endl;
//        }
//    }
