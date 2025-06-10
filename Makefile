CFLAGS = -Wall -IIncludes
LIBS = -lpthread
MOD_DIR = Modules

targets : nfs_manager nfs_client nfs_console

nfs_manager: $(MOD_DIR)/nfs_manager/nfs_manager.o \
			 $(MOD_DIR)/nfs_manager/manager_utils.o \
			 $(MOD_DIR)/nfs_manager/sync_info_mem_store.o \
			 Common/common.o
	gcc $(CFLAGS) -o nfs_manager $(MOD_DIR)/nfs_manager/nfs_manager.o \
								 $(MOD_DIR)/nfs_manager/manager_utils.o \
								 $(MOD_DIR)/nfs_manager/sync_info_mem_store.o \
								Common/common.o $(LIBS)

nfs_client: $(MOD_DIR)/nfs_client.o Common/common.o
	gcc $(CFLAGS) -o nfs_client $(MOD_DIR)/nfs_client.o Common/common.o $(LIBS)

nfs_console: $(MOD_DIR)/nfs_console.o Common/common.o
	gcc $(CFLAGS) -o nfs_console $(MOD_DIR)/nfs_console.o Common/common.o


clean:
	rm -f nfs_manager nfs_console nfs_client
	rm -f $(MOD_DIR)/*.o
	rm -f $(MOD_DIR)/nfs_manager/*.o
	rm -f Common/*.o