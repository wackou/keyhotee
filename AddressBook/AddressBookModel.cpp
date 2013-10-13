#include "AddressBookModel.hpp"
#include <QIcon>
#include <QPixmap>
#include <QImage>

#include <fc/reflect/variant.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/raw.hpp>



const QIcon& Contact::getIcon() const {return icon; }

Contact::Contact( const bts::addressbook::wallet_contact& contact )
: bts::addressbook::wallet_contact(contact)
{
   if( contact.icon_png.size() )
   {
        QImage image;
        if( image.loadFromData( (unsigned char*)icon_png.data(), icon_png.size() ) )
        {
            icon = QIcon( QPixmap::fromImage(image) );
        }
        else
        {
            wlog( "unable to load icon for contact ${c}", ("c",contact) );
        }
   }
   else
   {
      icon.addFile(QStringLiteral(":/images/user.png"), QSize(), QIcon::Normal, QIcon::Off);
   }
}



void Contact::setIcon( const QIcon& icon )
{
   this->icon = icon;
   if( !icon.isNull() )
   {
       QImage image;
       QByteArray byte_array;
       QBuffer buffer(&byte_array);
       buffer.open(QIODevice::WriteOnly);
       image.save(&buffer, "PNG"); // writes image into ba in PNG format
   
       icon_png.resize( byte_array.size() );
       memcpy( icon_png.data(), byte_array.data(), byte_array.size() );
   }
   else
   {
        icon_png.resize(0);
   }
}

QString Contact::getLabel()const
{
   QString label = (first_name + " " + last_name).c_str();
   if( label == " " )
   {
        return dac_id_string.c_str();
   }
   return label;
}


namespace Detail 
{
    class AddressBookModelImpl
    {
       public:
          QIcon                                   _default_icon;
          std::vector<Contact>                    _contacts;
          bts::addressbook::addressbook_ptr       _address_book;
          QStringListModel                        _contact_completion_model;
    };
}



AddressBookModel::AddressBookModel( QObject* parent, bts::addressbook::addressbook_ptr address_book )
:QAbstractTableModel(parent),my( new Detail::AddressBookModelImpl() )
{
   my->_address_book = address_book;
   my->_default_icon.addFile(QStringLiteral(":/images/user.png"), QSize(), QIcon::Normal, QIcon::Off);

   const std::unordered_map<uint32_t,bts::addressbook::wallet_contact>& loaded_contacts = address_book->get_contacts();
   my->_contacts.reserve( loaded_contacts.size() );
   QStringList completion_list;
   for( auto itr = loaded_contacts.begin(); itr != loaded_contacts.end(); ++itr )
   {
      auto contact = itr->second;
      ilog( "loading contacts..." );
      my->_contacts.push_back( Contact(contact) );

      //add dac_id to completion list
      completion_list.push_back( contact.dac_id_string.c_str() );
      //add fullname to completion list
      QString fullName = contact.first_name.c_str();
      fullName += " ";
      fullName += contact.last_name.c_str();
      completion_list.push_back(fullName);
   }
   my->_contact_completion_model.setStringList(completion_list);
}

AddressBookModel::~AddressBookModel()
{
}

int AddressBookModel::rowCount( const QModelIndex& parent )const
{
    return my->_contacts.size();
}

int AddressBookModel::columnCount( const QModelIndex& parent  )const
{
    return NumColumns;
}

bool AddressBookModel::removeRows( int row, int count, const QModelIndex& parent )
{
   return false;
}

QVariant AddressBookModel::headerData( int section, Qt::Orientation orientation, int role )const
{
    if( orientation == Qt::Horizontal )
    {
       switch( role )
       {
          case Qt::DecorationRole:
             switch( (Columns)section )
             {
                case UserIcon:
                    return my->_default_icon;
                default:
                   return QVariant();
             }
          case Qt::DisplayRole:
          {
              switch( (Columns)section )
              {
                 case FirstName:
                     return tr("First Name");
                 case LastName:
                     return tr("Last Name");
                 case Id:
                     return tr("Id");
                 case Age:
                     return tr("Age");
                 case Repute:
                     return tr("Repute");
                 case UserIcon:
                 case NumColumns:
                     break;
              }
          }
          case Qt::SizeHintRole:
              switch( (Columns)section )
              {
                  case UserIcon:
                      return QSize( 32, 16 );
                  default:
                      return QVariant();
              }
       }
    }
    else
    {
    }
    return QVariant();
}

QVariant AddressBookModel::data( const QModelIndex& index, int role )const
{
    if( !index.isValid() ) return QVariant();

    const Contact& current_contact = my->_contacts[index.row()];
    switch( role )
    {
       case Qt::SizeHintRole:
           switch( (Columns)index.column() )
           {
               case UserIcon:
                   return QSize( 48, 48 );
               default:
                   return QVariant();
           }
       case Qt::DecorationRole:
          switch( (Columns)index.column() )
          {
             case UserIcon:
                 return current_contact.getIcon();
             default:
                return QVariant();
          }
       case Qt::DisplayRole:
          switch( (Columns)index.column() )
          {
             case FirstName:
                 return current_contact.first_name.c_str();
             case LastName:
                 return current_contact.last_name.c_str();
             case Id:
                 return current_contact.dac_id_string.c_str();
             case Age:
                 return 0;
             case Repute:
                 return 0;

             case UserIcon:
             case NumColumns:
                return QVariant();
          }
    }
    return QVariant();
}

int AddressBookModel::storeContact( const Contact& contact_to_store )
{
   if( contact_to_store.wallet_index == WALLET_INVALID_INDEX )
   {
       auto num_contacts = my->_contacts.size();
       beginInsertRows( QModelIndex(), num_contacts, num_contacts );
          my->_contacts.push_back(contact_to_store);
          my->_contacts.back().wallet_index =  my->_contacts.size()-1;
       endInsertRows();
       my->_address_book->store_contact( my->_contacts.back() );
       return my->_contacts.back().wallet_index;
   }

   FC_ASSERT( contact_to_store.wallet_index < int(my->_contacts.size()) );
   auto row = contact_to_store.wallet_index;
   my->_contacts[row] = contact_to_store;
   my->_address_book->store_contact(  my->_contacts[row]  );

   Q_EMIT dataChanged( index( row, 0 ), index( row, NumColumns - 1) );
   return contact_to_store.wallet_index;
}

const Contact& AddressBookModel::getContactById( int contact_id )
{
   for( uint32_t i = 0; i < my->_contacts.size(); ++i )
   {
        if( my->_contacts[i].wallet_index == contact_id )
        {
            return my->_contacts[i];
        }
   }
   FC_ASSERT( !"invalid contact id" ); 
   //FC_ASSERT( !"invalid contact id ${id}", ("id",contact_id) );
}
const Contact& AddressBookModel::getContact( const QModelIndex& index  )
{
   FC_ASSERT(index.row() < (int)my->_contacts.size() );
   return my->_contacts[index.row()];
}

QStringListModel* AddressBookModel::GetContactCompletionModel()
{
  return &(my->_contact_completion_model);
}