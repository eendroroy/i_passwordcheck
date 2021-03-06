# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
  config.vm.box_check_update = true

  config.vm.network "private_network", ip: "192.168.33.10"

  config.vm.synced_folder ".", "/home/vagrant/passwordpolicy"

  config.vm.provider "virtualbox" do |vb|
    vb.gui = false
    vb.memory = 1024
  end

  config.vm.define 'centos7' do |centos|
    centos.vm.box = "centos/7"

    centos.vm.provision "bootstrap", type: "shell", run: 'never', inline: <<-SHELL
      yum --enablerepo=updates clean metadata
      yum -y update
      yum -y install openssl-devel
      rpm -Uvh https://yum.postgresql.org/10/redhat/rhel-7-x86_64/pgdg-centos10-10-2.noarch.rpm
      yum -y install postgresql10-server postgresql10-libs postgresql10-devel postgresql10-contrib
      yum -y install cracklib cracklib-devel cracklib-dicts words
      mkdict /usr/share/dict/* | packer /usr/lib/cracklib_dict
      # default data directory is '/var/lib/pgsql/10/data/'
      /usr/pgsql-10/bin/postgresql-10-setup initdb
      systemctl start postgresql-10.service
      systemctl enable postgresql-10.service
    SHELL
  end

  config.vm.define 'xenial' do |xenial|
    xenial.vm.box = "ubuntu/xenial64"
    xenial.vbguest.auto_update = false

    xenial.vm.provision "fix", type: "shell", run: "never", inline: <<-SHELL
      echo \"running as: `whoami`\"
      ls -la /etc/apt/sources.list
      sed \"s|http://archive.ubuntu.com/ubuntu|http://mirror.amberit.com.bd/ubuntu-archive|g\" -i /etc/apt/sources.list
    SHELL

    xenial.vm.provision "bootstrap", type: "shell", inline: <<-SHELL
      add-apt-repository 'deb http://apt.postgresql.org/pub/repos/apt/ xenial-pgdg main'
      wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | sudo apt-key add -
      apt-get -y update
      apt-get -y install make build-essential
      apt-get -y install postgresql postgresql-contrib libpq-dev postgresql-server-dev-all
      apt-get -y install libpam-cracklib libcrack2-dev
      systemctl start postgresql.service
      systemctl enable postgresql.service
    SHELL
  end

  config.vm.provision "install", type: "shell", run: 'never', inline: <<-SHELL
    cd /home/vagrant/passwordpolicy
    sudo PATH="/usr/pgsql-10/bin:$PATH" make
    sudo PATH="/usr/pgsql-10/bin:$PATH" make install
    sudo PATH="/usr/pgsql-10/bin:$PATH" make installcheck
    rm passwordpolicy.o passwordpolicy.so
  SHELL
end
