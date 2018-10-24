/**
* @brief 
* @file upload.c
* @author J.H. 
* @date 2018-10-17
*/

/* system includes */
#include <zephyr.h>
#include <kernel.h>
#include <shell/shell.h>
#include <string.h>
#include <stdlib.h>

/* local includes */
#include "upload.h"
#include "config.h"
#include "eth/sign.h"
#include "eth/transaction.h"
#include "zephyr/wallet.h"
#include "zephyr/sensor_service.h"
#include "zephyr/web3_rpc.h"
#include "helpers/hextobin.h"

// thread stack
#define UPLOAD_STACK_SIZE   512
K_THREAD_STACK_DEFINE(upload_stack, UPLOAD_STACK_SIZE);
static struct k_thread upload_thread_data;

// thread semaphore. it is taken when the thread is spawned and released on exit
K_SEM_DEFINE(upload_thread_sem, 1, 1);
K_SEM_DEFINE(upload_thread_exit_sem, 0, 1);

// create an eth transaction that contains the data and send it out
static int _send_data_eth(const address_t *addr, int32_t temperature, int32_t humidity)
{
    account_t *account = wallet_get_account();
    uint8_t tx_data[2] = {temperature, humidity};
    transaction_t tx;

    tx.nonce = account->nonce;
    tx.gas_price = 1 * 100000000;
    memcpy(&tx.to, addr, sizeof(tx.to));
    tx_set_value_u64(&tx, 0);
    tx.data = (uint8_t*)&tx_data;
    tx.data_len = sizeof(tx_data);
    tx.gas_limit = 21000 + 68 * sizeof(tx_data);

    #define BUFSIZE 256
    uint8_t buf[BUFSIZE];
    size_t txlen = tx_encode_sign(&tx, account->privkey.k, buf, BUFSIZE);

    if(web3_eth_sendRawTransaction(buf, txlen) < 0) {
        printk("Error: eth_sendRawTransaction()");
        return -1;
    }
    account->nonce++;

    return 0;
}

static void upload_main(const address_t addr)
{
    assert(k_sem_take(&upload_thread_sem, K_MSEC(1)) == 0);
    while(k_sem_count_get(&upload_thread_exit_sem) == 0) {
        int32_t temp, humidity;
        if(get_sensor_data(&temp, &humidity) == 0) {
            _send_data_eth(addr, temp, humidity);
        }
        k_sleep(5000);
    }
    k_sem_give(&upload_thread_sem);
}
/*K_THREAD_DEFINE(upload_main_id, 4096, upload_main, NULL, NULL, NULL, 7, 0, K_NO_WAIT);*/

static int upload_stop(const struct shell *shell, size_t argc, char *argv[])
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ARG_UNUSED(shell);
    if(k_sem_take(&upload_thread_sem, K_MSEC(5)) == 0) {
        printk("thread not running\n");
        k_sem_give(&upload_thread_sem);
        return 0;
    }
    // signal thread to exit
    k_sem_give(&upload_thread_exit_sem);
    // wait for the exit
    k_sem_take(&upload_thread_sem, K_FOREVER);
    // reset the semaphores
    k_sem_give(&upload_thread_sem);
    k_sem_take(&upload_thread_exit_sem, K_FOREVER);
    return 0;
}

static int upload_start(const struct shell *shell, size_t argc, char *argv[])
{
    ARG_UNUSED(shell);
    address_t addr;
    if(argc < 2) {
        printk("missing argument: address\n");
        return 0;
    }
    if(hextobin(argv[1], (uint8_t*)&addr, sizeof(addr)) < 0) {
        printk("invalid argument: address\n");
        return 0;
    }
    if(k_sem_take(&upload_thread_sem, K_MSEC(5)) != 0) {
        printk("thread already started\n");
        return 0;
    }
    k_thread_create(
            &upload_thread_data,
            upload_stack, K_THREAD_STACK_SIZEOF(upload_stack),
            (k_thread_entry_t)upload_main, addr, NULL, NULL,
            5, 0, K_NO_WAIT);
    return 0;
}

SHELL_CREATE_STATIC_SUBCMD_SET(sub_upload) {
    SHELL_CMD(start, NULL, "start upload service", upload_start),
    SHELL_CMD(stop, NULL, "stop upload service", upload_stop),
	SHELL_SUBCMD_SET_END
};
SHELL_CMD_REGISTER(upload, &sub_upload, "http upload service", NULL);
