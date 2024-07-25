`README.md`
`markdown`

# THA Core and Scripts Setup Guide

This guide will help you set up THA Core and the associated scripts for monitoring and automating transactions.

## Step 1: Install Dependencies

***Ensure Python and Git are installed on your system***

```
bash
Copy code
```
sudo apt update
sudo apt install python3 python3-pip git
```

## Step 2: Clone THA Core Repository:

***Use Git to clone the THA Core repository from GitHub***

`bash`
`Copy code`
```
gh repo clone nucash-mining/tha-mining
cd tha-mining
```

## Step 3: Build and Install THA Core:

***Follow the installation instructions provided in the repository***

`bash`
`Copy code`
```
./configure
make
sudo make install
```


## Step 4: Set Up SEND and RECEIVE Wallets

***Initialize the THA Core Daemon***

Start the THA Core daemon to interact with the blockchain:

`bash`
`Copy code`
```
thad -daemon
Create SEND and RECEIVE Wallets
```

***If THA Core does not automatically create default wallets, create them manually***

`bash`
`Copy code`
```
tha-cli createwallet "SEND"
tha-cli createwallet "RECEIVE"
Generate Wallet Addresses
```

***Generate new addresses for both wallets to use for transactions***

`bash`
`Copy code`
```
send_address=$(tha-cli -rpcwallet=SEND getnewaddress)
receive_address=$(tha-cli -rpcwallet=RECEIVE getnewaddress)
echo "SEND Wallet Address: $send_address"
echo "RECEIVE Wallet Address: $receive_address"
```

## Step 5: Deploy the Python Scripts

***Transaction Listener Script***
Create a file named TransactionListener.py and add the following content:

`python`
`Copy code`
```
import requests
import json
import time

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_name, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_name}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def list_transactions(self, count=10, skip=0, include_watch_only=True):
        return self.call('listtransactions', ["*", count, skip, include_watch_only])

    def get_transaction(self, txid):
        return self.call('gettransaction', [txid])

if __name__ == '__main__':
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_name = 'SEND'
    client = THARpcClient(rpc_user, rpc_password, wallet_name)

    try:
        print("Listening for new transactions...")
        while True:
            transactions = client.list_transactions()
            for tx in transactions:
                if tx['category'] == 'receive' and tx['confirmations'] >= 1:
                    print(f"Transaction {tx['txid']} confirmed. Proceeding to next steps.")
                    # Here, trigger the next steps or exit loop
                    # Example: os.system('python3 path_to_next_script.py')
            time.sleep(60)  # Wait for a minute before checking again
    except Exception as e:
        print(f"An error occurred: {e}")
```

***Send THA Transaction Script***
Create a file named SendTHATransaction.py and add the following content:

`python`
`Copy code`
```
import requests
import json

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_path, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_path}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def send_to_address(self, address, amount):
        return self.call('sendtoaddress', [address, amount])

if __name__ == '__main__':
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_path = 'SEND'
    recipient_address = '1BSxJvD1iGv9UYHFU1uTVvxD7L4kT6W58s'
    amount = 0.1

    client = THARpcClient(rpc_user, rpc_password, wallet_path)

    try:
        # Send 0.1 THA to the recipient address
        txid = client.send_to_address(recipient_address, amount)
        print(f"Transaction ID: {txid}")

    except Exception as e:
        print(f"An error occurred: {e}")
```        
***Block Reward Confirmation Trigger Script***
Create a file named BlockRewardConfirmationTrigger.py and add the following content:

`python`
`Copy code`
```
import requests
import json
import time

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_name, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_name}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def list_transactions(self, count, skip=0):
        return self.call('listtransactions', ["*", count, skip, True])

    def get_transaction(self, txid):
        return self.call('gettransaction', [txid])

def trigger_transactions_script():
    # This function would trigger the script to execute 500 transactions over 10 minutes
    print("Triggering transactions script...")
    # You can import and call another Python script here or execute a shell command
    # Example: os.system('python3 send_transactions.py')

if __name__ == '__main__':
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_name = 'SEND'
    expected_amount = 50
    client = THARpcClient(rpc_user, rpc_password, wallet_name)

    try:
        while True:
            transactions = client.list_transactions(10)
            for tx in transactions:
                if tx['category'] == 'receive' and tx['amount'] == expected_amount:
                    tx_details = client.get_transaction(tx['txid'])
                    if tx_details['confirmations'] >= 1:
                        trigger_transactions_script()
                        break
            time.sleep(60)
    except Exception as e:
        print(f"An error occurred: {e}")
```
***Mining Reward Redirector Script***
Create a file named MiningRewardRedirector.py and add the following content:

`python`
`Copy code`
```
import requests
import json
import time

class THARpcClient:
    def __init__(self, rpc_user, rpc_password, wallet_name, rpc_host='127.0.0.1', rpc_port=18332):
        self.url = f'http://{rpc_host}:{rpc_port}/wallet/{wallet_name}'
        self.headers = {'content-type': 'application/json'}
        self.auth = (rpc_user, rpc_password)

    def call(self, method, params=[]):
        payload = json.dumps({"method": method, "params": params, "jsonrpc": "2.0", "id": 0})
        response = requests.post(self.url, headers=self.headers, auth=self.auth, data=payload)
        if response.status_code == 200:
            return response.json()['result']
        else:
            raise Exception(f"Error calling RPC method {method}: {response.status_code} - {response.text}")

    def list_transactions(self, count, skip=0):
        return self.call('listtransactions', ["*", count, skip, True])

    def get_transaction(self, txid):
        return self.call('gettransaction', [txid])

    def send_to_address(self, address, amount):
        return self.call('sendtoaddress', [address, amount])

if __name__ == '__main__':
    rpc_user = input('Enter RPC username: ')
    rpc_password = input('Enter RPC password: ')
    wallet_name = 'RECEIVE'
    send_address = '1HXJUf7zwHZ5K2vhf9fMueMkXzjMUgDv6k'
    client = THARpcClient(rpc_user, rpc_password, wallet_name)

    try:
        while True:
            transactions = client.list_transactions(10)
            for tx in transactions:
                if tx['category'] == 'receive' and tx['confirmations'] == 1:
                    txid = tx['txid']
                    transaction = client.get_transaction(txid)
                    if transaction['details'][0]['category'] == 'generate':
                        amount = transaction['amount']
                        send_txid = client.send_to_address(send_address, amount)
                        print(f"Sent {amount} THA back to {send_address}. Transaction ID: {send_txid}")
            time.sleep(60)
    except Exception as e:
        print(f"An error occurred: {e}")
```

## Step 6: Automate Execution with a Bash Script:
***Create a file named setup_and_monitor.sh and add the following content:***

`bash`
`Copy code`
```
#!/bin/bash

# Install Python and Git
sudo apt update
sudo apt install -y python3 python3-pip git

# Prompt for user input
echo "Enter RPC username: "
read rpc_user
echo "Enter RPC password: "
read -s rpc_password

# Clone THA Core
gh repo clone nucash-mining/tha-mining
cd tha-mining

# Build and install THA Core
./configure
make
sudo make install

# Start THA daemon
thad -daemon

# Create wallets and generate addresses
tha-cli createwallet "SEND"
tha-cli createwallet "RECEIVE"
send_address=$(tha-cli -rpcwallet=SEND getnewaddress)
receive_address=$(tha-cli -rpcwallet=RECEIVE getnewaddress)

# Save the wallet addresses to a file
echo "SEND Wallet Address: $send_address" > wallet_addresses.txt
echo "RECEIVE Wallet Address: $receive_address" >> wallet_addresses.txt

# Save the user input to environment variables for the Python scripts
export RPC_USER=$rpc_user
export RPC_PASSWORD=$rpc_password

# Run the transaction listener script
python3 TransactionListener.py
```
***Make the script executable:***

`bash`
`Copy code`
```
chmod +x setup_and_monitor.sh
```
***Run your setup script:***

`bash`
`Copy code`
```
./setup_and_monitor.sh
```

## Step 7: Document Everything

***Prepare a README.md:***

`markdown`
`Copy code`
# THA Mining and Transaction Automation Scripts

***This repository contains a collection of scripts designed to automate various tasks related to THA cryptocurrency operations, such as monitoring wallets for new block rewards and automating transactions.***

## Folder Structure

- `mining_scripts/`: Contains scripts that handle the detection and forwarding of mining rewards.
- `transaction_scripts/`: Contains scripts designed to automate the sending of multiple transactions.

## Scripts Description

### Mining Scripts

- **MiningRewardRedirector.py**
  - **Purpose**: Monitors the RECEIVE wallet for new block rewards and forwards them to a specific address.
  - **Usage**:
    `bash`
```
    python3 MiningRewardRedirector.py
```

- **BlockRewardConfirmationTrigger.py**
  - **Purpose**: Listens for incoming transactions of newly mined coins to the SEND wallet and triggers a batch transaction process.
  - **Usage**:
    `bash`
```
    python3 BlockRewardConfirmationTrigger.py
```

### Transaction Scripts

- **SendTHATransaction.py**
  - **Purpose**: Sends 0.1 THA to the recipient address.
  - **Usage**:
    
    `bash`
```
    python3 SendTHATransaction.py
```

- **TransactionListener.py**
  - **Purpose**: Listens for incoming transactions to the SEND wallet and waits for one confirmation.
  - **Usage**:
    `bash`
    ```
    python3 TransactionListener.py
    ```

## Setup


***
1. Ensure Python 3.x is installed on your system.
2. Install required Python packages:***

`bash`
```
pip install requests
```

## Configure your THA node settings in each script as needed, especially the RPC user, password, and wallet names.
***Contributing***
Feel free to fork this repository and submit pull requests to contribute to the development of these automation scripts. For major changes, please open an issue first to discuss what you would like to change.

## License
***MIT***


```
vbnet
```
`Copy code`

### Conclusion

By following the above guide, users can install dependencies, set up their THA Core environment, and automate transactions with the provided scripts. This README.md will be accessible on your GitHub repository, and users can follow it step-by-step to replicate your setup.
 ***The `setup_and_monitor.sh` script will automate the entire process, reducing the manual effort required.***
